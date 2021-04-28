//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
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
	)

	for name, tc := range map[string]struct {
		rankInSuperblock bool
		instanceIdx      uint32
		exitErr          error
		expShouldForward bool
		expEvtMsg        string
	}{
		"without rank": {
			expEvtMsg: "DAOS engine 0 exited unexpectedly",
		},
		"with rank": {
			rankInSuperblock: true,
			expShouldForward: true,
			expEvtMsg:        "DAOS engine 0 exited unexpectedly",
		},
		"instance 1": {
			instanceIdx: 1,
			expEvtMsg:   "DAOS engine 1 exited unexpectedly",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			rxEvts = []*events.RASEvent{}
			runner := engine.NewRunner(log, &engine.Config{})
			engine := NewEngineInstance(log, nil, nil, nil, runner)
			engine.setIndex(tc.instanceIdx)

			if tc.rankInSuperblock {
				engine.setSuperblock(&Superblock{
					Rank: system.NewRankPtr(0), ValidRank: true,
				})
			}

			engine.OnInstanceExit(publishInstanceExitFn(fakePublish,
				hostname()))

			engine.exit(context.Background(), exitErr)

			common.AssertEqual(t, 1, len(rxEvts),
				"unexpected number of events published")
			common.AssertEqual(t, rxEvts[0].ShouldForward(),
				tc.expShouldForward, "unexpected forwarding state")
			if diff := cmp.Diff(tc.expEvtMsg, rxEvts[0].Msg); diff != "" {
				t.Fatalf("unexpected event message (-want, +got):\n%s\n", diff)
			}

		})
	}
}
