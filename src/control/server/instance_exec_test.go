//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	sysprov "github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// TestIOEngineInstance_exit establishes that event is published on exit.
func TestIOEngineInstance_exit(t *testing.T) {
	var (
		rxEvts      []*events.RASEvent
		fakePublish = func(evt *events.RASEvent) {
			rxEvts = append(rxEvts, evt)
		}
		exitErr = errors.New("killed")
		exitMsg = "DAOS engine %d exited unexpectedly: killed"
	)

	for name, tc := range map[string]struct {
		trc              *engine.TestRunnerConfig
		rankInSuperblock bool
		instanceIdx      uint32
		exitErr          error
		expShouldForward bool
		expEvtMsg        string
		expExPid         int
	}{
		"without rank": {
			expEvtMsg: fmt.Sprintf(exitMsg, 0),
		},
		"with rank": {
			rankInSuperblock: true,
			expShouldForward: true,
			expEvtMsg:        fmt.Sprintf(exitMsg, 0),
		},
		"instance 1": {
			instanceIdx: 1,
			expEvtMsg:   fmt.Sprintf(exitMsg, 1),
		},
		"with pid": {
			trc:       &engine.TestRunnerConfig{LastPid: 1234},
			expEvtMsg: fmt.Sprintf(exitMsg, 0),
			expExPid:  1234,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			rxEvts = []*events.RASEvent{}

			runner := engine.NewTestRunner(tc.trc, &engine.Config{})

			engine := NewEngineInstance(log, nil, nil, runner, nil)
			engine.setIndex(tc.instanceIdx)

			if tc.rankInSuperblock {
				engine.setSuperblock(&Superblock{
					Rank: ranklist.NewRankPtr(0), ValidRank: true,
				})
			}
			if tc.expExPid == 0 {
				tc.expExPid = os.Getpid()
			}

			hn, _ := os.Hostname()
			engine.OnInstanceExit(createPublishInstanceExitFunc(fakePublish, hn))

			engine.handleExit(test.Context(t), tc.expExPid, exitErr)

			test.AssertEqual(t, 1, len(rxEvts),
				"unexpected number of events published")
			test.AssertEqual(t, rxEvts[0].ShouldForward(),
				tc.expShouldForward, "unexpected forwarding state")
			if diff := cmp.Diff(tc.expEvtMsg, rxEvts[0].Msg); diff != "" {
				t.Fatalf("unexpected message (-want, +got):\n%s\n", diff)
			}
			if tc.expExPid != rxEvts[0].ProcID {
				t.Fatalf("unexpected PID in event (%d != %d)", tc.expExPid, rxEvts[0].ProcID)
			}
		})
	}
}

// runnableEngineConfig returns an engine config whose storage needs no format.
func runnableEngineConfig(mnt string) *engine.Config {
	return engine.MockConfig().WithStorage(
		storage.NewTierConfig().
			WithStorageClass(storage.ClassDcpm.String()).
			WithScmMountPoint(mnt).
			WithScmDeviceList("/dev/foo"),
	)
}

// newRunnableTestInstance returns an instance whose format() completes without waiting (storage
// mounted, no format needed, superblock set) so Run() proceeds straight to the runner.
func newRunnableTestInstance(t *testing.T, log logging.Logger, runner EngineRunner, joinFn systemJoinFn) *EngineInstance {
	t.Helper()

	msc := &sysprov.MockSysConfig{IsMountedBool: true, GetfsStr: "ext4"}
	mp := storage.NewProvider(log, 0, &runner.GetConfig().Storage,
		sysprov.NewMockSysProvider(log, msc),
		scm.NewMockProvider(log, &scm.MockBackendConfig{}, msc),
		nil, &storage.MockMetadataProvider{})

	ei := NewEngineInstance(log, mp, joinFn, runner, nil)
	ei.setSuperblock(&Superblock{Rank: ranklist.NewRankPtr(0), ValidRank: true})

	return ei
}

// killedOnCancelRunner delivers its exit the way the real runner does once its context is
// cancelled: unconditionally, closing the channel afterwards.
type killedOnCancelRunner struct {
	*engine.TestRunner
	exit   *engine.RunnerExitInfo
	starts uint32
	stop   chan struct{} // closed at test end to release an exit nobody read
}

func (r *killedOnCancelRunner) Start(ctx context.Context) (engine.RunnerExitChan, error) {
	atomic.AddUint32(&r.starts, 1)

	exitCh := make(engine.RunnerExitChan)
	go func() {
		<-ctx.Done()
		select {
		case exitCh <- r.exit:
			close(exitCh)
		case <-r.stop:
		}
	}()

	return exitCh, nil
}

func waitFor(t *testing.T, timeout time.Duration, msg string, cond func() bool) {
	t.Helper()

	deadline := time.Now().Add(timeout)
	for !cond() {
		if time.Now().After(deadline) {
			t.Fatal(msg)
		}
		time.Sleep(10 * time.Millisecond)
	}
}

// TestServer_EngineInstance_Run_exitBeforeReady establishes that an engine process exiting
// before it sends its ready notification is handled like any other exit, and that the instance
// can be started again afterwards.
func TestServer_EngineInstance_Run_exitBeforeReady(t *testing.T) {
	type exitInfo struct {
		pid int
		err error
	}
	recordExits := func(ei *EngineInstance) chan exitInfo {
		exits := make(chan exitInfo, 8)
		ei.OnInstanceExit(func(_ context.Context, _ uint32, _ ranklist.Rank, _ uint64, exitErr error, exPid int) error {
			exits <- exitInfo{pid: exPid, err: exitErr}
			return nil
		})
		return exits
	}
	nextExit := func(t *testing.T, exits chan exitInfo, msg string) exitInfo {
		t.Helper()
		select {
		case got := <-exits:
			return got
		case <-time.After(2 * time.Second):
			t.Fatal(msg)
		}
		return exitInfo{}
	}
	failJoin := func(context.Context, *control.SystemJoinReq) (*control.SystemJoinResp, error) {
		return nil, errors.New("join failed")
	}

	t.Run("exit is handled and restart is deliverable", func(t *testing.T) {
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)

		var starts uint32
		ei := newRunnableTestInstance(t, log, engine.NewTestRunner(&engine.TestRunnerConfig{
			StartCb:        func() { atomic.AddUint32(&starts, 1) },
			RunnerExitInfo: &engine.RunnerExitInfo{Error: errors.New("boom"), PID: 1234},
		}, runnableEngineConfig("/mnt/test")), nil)
		exits := recordExits(ei)

		ctx, cancel := context.WithTimeout(test.Context(t), 5*time.Second)
		defer cancel()

		ei.Run(ctx)

		got := nextExit(t, exits, "engine exit was never handled")
		test.AssertEqual(t, 1234, got.pid, "unexpected pid passed to exit callback")
		test.CmpErr(t, errors.New("boom"), got.err)
		test.CmpErr(t, errors.New("boom"), ei.lastErr())
		for _, want := range []string{"pid 1234", "exited with status: boom"} {
			if !strings.Contains(buf.String(), want) {
				t.Fatalf("expected %q in log", want)
			}
		}
		if strings.Contains(buf.String(), "removing socket file") {
			t.Fatal("socket removal error logged for an engine that never had a socket")
		}

		reqCtx, reqCancel := context.WithTimeout(ctx, time.Second)
		defer reqCancel()
		ei.requestStart(reqCtx)
		if reqCtx.Err() != nil {
			t.Fatal("requestStart blocked: instance wedged")
		}
		waitFor(t, 2*time.Second, "engine was not started again", func() bool {
			return atomic.LoadUint32(&starts) == 2
		})
	})

	t.Run("ready queued by the exited engine is discarded", func(t *testing.T) {
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)

		var starts uint32
		ei := newRunnableTestInstance(t, log, engine.NewTestRunner(&engine.TestRunnerConfig{
			StartCb: func() { atomic.AddUint32(&starts, 1) },
			RunnerExitInfoCb: func(ctx context.Context) *engine.RunnerExitInfo {
				if atomic.LoadUint32(&starts) > 1 {
					<-ctx.Done() // the second engine stays up
				}
				return &engine.RunnerExitInfo{Error: errors.New("boom"), PID: 1234}
			},
		}, runnableEngineConfig("/mnt/test")), failJoin)
		exits := recordExits(ei)

		ctx, cancel := context.WithTimeout(test.Context(t), 5*time.Second)
		defer cancel()

		ei.Run(ctx)
		nextExit(t, exits, "engine exit was never handled")

		// The exited engine sent its ready notification just before it died.
		stale := getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0)
		stale.Incarnation = 777
		ei.NotifyDrpcReady(stale)
		time.Sleep(50 * time.Millisecond) // let the notification queue

		ei.requestStart(ctx)
		waitFor(t, 2*time.Second, "engine was not started again", func() bool {
			return atomic.LoadUint32(&starts) == 2
		})
		time.Sleep(200 * time.Millisecond)
		if strings.Contains(buf.String(), "incarnation=777") {
			t.Fatal("ready notification from the exited engine was used for the next start")
		}
	})

	t.Run("engine killed after a failed startup is reaped", func(t *testing.T) {
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)

		// A real directory, so the superblock write succeeds and the join is what fails.
		testDir, cleanup := test.CreateTestDir(t)
		defer cleanup()

		stop := make(chan struct{})
		defer close(stop)
		runner := &killedOnCancelRunner{
			TestRunner: engine.NewTestRunner(nil, runnableEngineConfig(testDir)),
			exit:       &engine.RunnerExitInfo{Error: errors.New("killed"), PID: 4321},
			stop:       stop,
		}
		ei := newRunnableTestInstance(t, log, runner, failJoin)
		exits := recordExits(ei)

		ctx, cancel := context.WithTimeout(test.Context(t), 5*time.Second)
		defer cancel()

		ei.Run(ctx)
		waitFor(t, 2*time.Second, "engine was not started", func() bool {
			return atomic.LoadUint32(&runner.starts) == 1
		})
		ei.NotifyDrpcReady(getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0))

		got := nextExit(t, exits, "failed startup was never handled")
		test.AssertEqual(t, 0, got.pid, "unexpected pid for the startup failure")
		test.CmpErr(t, errors.New("join failed"), got.err)

		got = nextExit(t, exits, "engine killed after the failed startup was never reaped")
		test.AssertEqual(t, 4321, got.pid, "unexpected pid for the killed engine")
		test.CmpErr(t, errors.New("killed"), got.err)

		reqCtx, reqCancel := context.WithTimeout(ctx, time.Second)
		defer reqCancel()
		ei.requestStart(reqCtx)
		if reqCtx.Err() != nil {
			t.Fatal("requestStart blocked after a failed startup")
		}
		waitFor(t, 2*time.Second, "engine was not started again", func() bool {
			return atomic.LoadUint32(&runner.starts) == 2
		})
	})

	t.Run("shutdown before ready", func(t *testing.T) {
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)

		var starts uint32
		ei := newRunnableTestInstance(t, log, engine.NewTestRunner(&engine.TestRunnerConfig{
			StartCb: func() { atomic.AddUint32(&starts, 1) },
			RunnerExitInfoCb: func(ctx context.Context) *engine.RunnerExitInfo {
				<-ctx.Done()
				return &engine.RunnerExitInfo{Error: errors.New("ending")}
			},
		}, runnableEngineConfig("/mnt/test")), nil)

		ctx, cancel := context.WithCancel(test.Context(t))
		defer cancel()

		ei.Run(ctx)

		waitFor(t, 2*time.Second, "engine was not started", func() bool {
			return atomic.LoadUint32(&starts) == 1
		})
		cancel()
		waitFor(t, 2*time.Second, "shutdown was not handled", func() bool {
			return ei.lastErr() != nil
		})
		// Cancellation and the runner's exit race; either must be recorded, nothing else.
		if err := ei.lastErr(); !errors.Is(err, context.Canceled) && !strings.Contains(err.Error(), "ending") {
			t.Fatalf("unexpected exit error: %v", err)
		}
	})
}

func TestServer_EngineInstance_initIncarnationFromSuperblock(t *testing.T) {
	for name, tc := range map[string]struct {
		startIncarnation uint64
		superblock       *Superblock
		expIncarnation   uint64
		expErr           error
	}{
		"nil superblock": {
			expErr: errors.New("no superblock found"),
		},
		"incarnation already set": {
			startIncarnation: 1,
			superblock: &Superblock{
				Incarnation: 2,
			},
			expIncarnation: 1,
		},
		"success": {
			superblock: &Superblock{
				Incarnation: 2,
			},
			expIncarnation: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			ei := newTestEngine(logging.FromContext(ctx), false, nil)
			ei.incarnation = tc.startIncarnation
			ei.setSuperblock(tc.superblock)

			err := ei.initIncarnationFromSuperblock()

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expIncarnation, ei.incarnation, "")
		})
	}
}
