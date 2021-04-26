//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

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
		exitErr          error
		expShouldForward bool
	}{
		"without rank": {},
		"with rank": {
			rankInSuperblock: true,
			expShouldForward: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			rxEvts = []*events.RASEvent{}
			runner := engine.NewRunner(log, &engine.Config{})
			engine := NewEngineInstance(log, nil, nil, nil, runner)

			if tc.rankInSuperblock {
				engine.setSuperblock(&Superblock{
					Rank: system.NewRankPtr(0), ValidRank: true,
				})
			}

			engine.OnInstanceExit(publishInstanceExitFn(fakePublish,
				hostname(), engine.Index()))

			engine.exit(context.Background(), exitErr)

			common.AssertEqual(t, 1, len(rxEvts),
				"unexpected number of events published")
			common.AssertEqual(t, rxEvts[0].ShouldForward(),
				tc.expShouldForward, "unexpected forwarding state")
		})
	}
}
