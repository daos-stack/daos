//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
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
		exitErr          error
		expShouldForward bool
		expEvtMsg        string
		expExPid         uint64
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
			defer common.ShowBufferOnFailure(t, buf)

			rxEvts = []*events.RASEvent{}

			runner := engine.NewTestRunner(tc.trc, &engine.Config{})

			engine := NewEngineInstance(log, nil, nil, nil, runner)
			engine.setIndex(tc.instanceIdx)

			if tc.rankInSuperblock {
				engine.setSuperblock(&Superblock{
					Rank: system.NewRankPtr(0), ValidRank: true,
				})
			}
			if tc.expExPid == 0 {
				tc.expExPid = uint64(os.Getpid())
			}

			engine.OnInstanceExit(publishInstanceExitFn(fakePublish,
				hostname()))

			engine.exit(context.Background(), exitErr)

			common.AssertEqual(t, 1, len(rxEvts),
				"unexpected number of events published")
			common.AssertEqual(t, rxEvts[0].ShouldForward(),
				tc.expShouldForward, "unexpected forwarding state")
			if diff := cmp.Diff(tc.expEvtMsg, rxEvts[0].Msg); diff != "" {
				t.Fatalf("unexpected message (-want, +got):\n%s\n", diff)
			}
			common.AssertEqual(t, tc.expExPid, rxEvts[0].ProcID,
				"unexpected process ID in event")
		})
	}
}
