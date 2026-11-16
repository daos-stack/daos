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

// newRunnableTestInstance returns an instance whose format() completes without waiting (storage
// mounted, no format needed, superblock set) so Run() proceeds straight to the runner.
func newRunnableTestInstance(t *testing.T, log logging.Logger, trc *engine.TestRunnerConfig) *EngineInstance {
	t.Helper()

	cfg := engine.MockConfig().WithStorage(
		storage.NewTierConfig().
			WithStorageClass(storage.ClassDcpm.String()).
			WithScmMountPoint("/mnt/test").
			WithScmDeviceList("/dev/foo"),
	)
	msc := &sysprov.MockSysConfig{IsMountedBool: true, GetfsStr: "ext4"}
	mp := storage.NewProvider(log, 0, &cfg.Storage,
		sysprov.NewMockSysProvider(log, msc),
		scm.NewMockProvider(log, &scm.MockBackendConfig{}, msc),
		nil, &storage.MockMetadataProvider{})

	ei := NewEngineInstance(log, mp, nil, engine.NewTestRunner(trc, cfg), nil)
	ei.setSuperblock(&Superblock{Rank: ranklist.NewRankPtr(0), ValidRank: true})

	return ei
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
	t.Run("exit is handled and restart is deliverable", func(t *testing.T) {
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)

		var starts uint32
		ei := newRunnableTestInstance(t, log, &engine.TestRunnerConfig{
			StartCb:        func() { atomic.AddUint32(&starts, 1) },
			RunnerExitInfo: &engine.RunnerExitInfo{Error: errors.New("boom"), PID: 1234},
		})

		type exitInfo struct {
			pid int
			err error
		}
		exits := make(chan exitInfo, 2)
		ei.OnInstanceExit(func(_ context.Context, _ uint32, _ ranklist.Rank, _ uint64, exitErr error, exPid int) error {
			exits <- exitInfo{pid: exPid, err: exitErr}
			return nil
		})

		ctx, cancel := context.WithTimeout(test.Context(t), 5*time.Second)
		defer cancel()

		ei.Run(ctx)

		var got exitInfo
		select {
		case got = <-exits:
		case <-time.After(2 * time.Second):
			t.Fatal("engine exit was never handled")
		}
		test.AssertEqual(t, 1234, got.pid, "unexpected pid passed to exit callback")
		test.CmpErr(t, errors.New("boom"), got.err)
		test.CmpErr(t, errors.New("boom"), ei.lastErr())
		for _, want := range []string{"pid 1234", "exited with status: boom"} {
			if !strings.Contains(buf.String(), want) {
				t.Fatalf("expected %q in log", want)
			}
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

	t.Run("shutdown before ready", func(t *testing.T) {
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)

		var starts uint32
		ei := newRunnableTestInstance(t, log, &engine.TestRunnerConfig{
			StartCb: func() { atomic.AddUint32(&starts, 1) },
			RunnerExitInfoCb: func(ctx context.Context) *engine.RunnerExitInfo {
				<-ctx.Done()
				return &engine.RunnerExitInfo{Error: errors.New("ending")}
			},
		})

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
