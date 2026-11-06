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
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/system"
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
		cleanExit        bool
		stopRequested    bool
		expNoEvent       bool
		expShouldForward bool
		expEvtMsg        string
		expExPid         int
	}{
		"without rank": {
			expEvtMsg: fmt.Sprintf(exitMsg, 0),
		},
		"clean exit after a stop request": {
			cleanExit:     true,
			stopRequested: true,
			expNoEvent:    true,
		},
		"clean exit without a stop request": {
			cleanExit: true,
			expEvtMsg: "DAOS engine 0 exited unexpectedly: exited without a stop request",
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
			engine.stopRequested.Store(tc.stopRequested)

			err := exitErr
			if tc.cleanExit {
				err = nil
			}
			engine.handleExit(test.Context(t), tc.expExPid, err)

			if tc.expNoEvent {
				test.AssertEqual(t, 0, len(rxEvts), "unexpected event published")
				test.AssertEqual(t, nil, engine.getExitErr(), "last error")
				return
			}
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

func TestServer_EngineInstance_awaitStart(t *testing.T) {
	for name, tc := range map[string]struct {
		held        bool
		notify      bool
		abort       bool
		notifyAfter bool // release attempted after the abort
		expHold     bool // the hold callback (preflight) runs
		expErr      error
		expState    system.MemberState
	}{
		"not held": {
			expState: system.MemberStateStopped,
		},
		"released by format --start before the gate": {
			held:     true,
			notify:   true,
			expState: system.MemberStateStopped,
		},
		"held then aborted stays held": {
			held:     true,
			abort:    true,
			expErr:   errStartAborted,
			expState: system.MemberStateAwaitStart,
		},
		"abort replaces a parked release": {
			held:     true,
			notify:   true,
			abort:    true,
			expErr:   errStartAborted,
			expState: system.MemberStateAwaitStart,
		},
		"release does not replace a parked abort": {
			held:        true,
			abort:       true,
			notifyAfter: true,
			expErr:      errStartAborted,
			expState:    system.MemberStateAwaitStart,
		},
		"held until timeout": {
			held:     true,
			expHold:  true,
			expErr:   context.DeadlineExceeded,
			expState: system.MemberStateAwaitStart,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			runner := engine.NewTestRunner(nil, engine.MockConfig())
			ei := NewEngineInstance(log, nil, nil, runner, nil)
			held := false
			ei.holdStart(func() { held = true })
			ei.waitStart.Store(tc.held)
			if tc.held {
				test.AssertEqual(t, system.MemberStateAwaitStart, ei.LocalState(), "")
			}
			if tc.notify {
				ei.NotifyStartReady()
			}
			if tc.abort {
				ei.abortStart()
			}
			if tc.notifyAfter {
				ei.NotifyStartReady()
			}

			ctx, cancel := context.WithTimeout(test.Context(t), 100*time.Millisecond)
			defer cancel()

			test.CmpErr(t, tc.expErr, ei.awaitStart(ctx))
			test.AssertEqual(t, tc.expState, ei.LocalState(), "")
			test.AssertEqual(t, tc.expHold, held, "hold callback")
		})
	}
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

func TestServer_EngineInstance_waitReady_exitBeforeReady(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	var rxEvts []*events.RASEvent
	runner := engine.NewTestRunner(nil, engine.MockConfig())
	ei := NewEngineInstance(log, nil, nil, runner, nil)
	hn, _ := os.Hostname()
	ei.OnInstanceExit(createPublishInstanceExitFunc(func(evt *events.RASEvent) {
		rxEvts = append(rxEvts, evt)
	}, hn))

	exitCh := make(chan *engine.RunnerExitInfo, 1)
	exitCh <- &engine.RunnerExitInfo{PID: 42, Error: errors.New("killed")}

	ctx, cancel := context.WithTimeout(test.Context(t), 100*time.Millisecond)
	defer cancel()

	test.CmpErr(t, errExitedBeforeReady, ei.waitReady(ctx, exitCh))
	test.AssertEqual(t, 1, len(rxEvts), "exit event")
	test.AssertEqual(t, 42, rxEvts[0].ProcID, "exit pid")
}
