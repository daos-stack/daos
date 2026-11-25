//
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestSystem_MemberState_isTransitionIllegal(t *testing.T) {
	nextState := func(state MemberState) MemberState {
		if state == MemberStateUnknown {
			return state + 1
		}
		return state << 1
	}

	for name, tc := range map[string]struct {
		start          MemberState
		expLegalStates []MemberState
	}{
		"unknown": {
			start:          MemberStateUnknown,
			expLegalStates: []MemberState{},
		},
		"admin excluded": {
			start:          MemberStateAdminExcluded,
			expLegalStates: []MemberState{},
		},
		"await format": {
			start: MemberStateAwaitFormat,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateCheckerStarted,
				MemberStateErrored,
				MemberStateJoined,
				MemberStateReady,
				MemberStateStarting,
				MemberStateStopped,
				MemberStateStopping,
				MemberStateUnresponsive,
				MemberStateUnknown,
			},
		},
		"starting": {
			start: MemberStateStarting,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateCheckerStarted,
				MemberStateErrored,
				MemberStateJoined,
				MemberStateReady,
				MemberStateStopped,
				MemberStateStopping,
				MemberStateUnresponsive,
				MemberStateUnknown,
			},
		},
		"ready": {
			start: MemberStateReady,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateCheckerStarted,
				MemberStateErrored,
				MemberStateJoined,
				MemberStateStarting,
				MemberStateStopped,
				MemberStateStopping,
				MemberStateUnresponsive,
				MemberStateUnknown,
			},
		},
		"joined": {
			start: MemberStateJoined,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateCheckerStarted,
				MemberStateErrored,
				MemberStateExcluded,
				MemberStateStarting,
				MemberStateStopped,
				MemberStateStopping,
				MemberStateUnresponsive,
				MemberStateUnknown,
			},
		},
		"errored": {
			start: MemberStateErrored,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateCheckerStarted,
				MemberStateExcluded,
				MemberStateStarting,
				MemberStateStopped,
				MemberStateUnresponsive,
				MemberStateUnknown,
			},
		},
		"unresponsive": {
			start: MemberStateUnresponsive,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateCheckerStarted,
				MemberStateErrored,
				MemberStateExcluded,
				MemberStateStarting,
				MemberStateStopped,
				MemberStateUnknown,
			},
		},
		"checker": {
			start: MemberStateCheckerStarted,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateErrored,
				MemberStateExcluded,
				MemberStateStarting,
				MemberStateStopping,
				MemberStateStopped,
				MemberStateUnresponsive,
				MemberStateUnknown,
			},
		},
		"excluded": {
			start: MemberStateExcluded,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateJoined,
				MemberStateCheckerStarted,
				MemberStateUnknown,
			},
		},
		"stopping": {
			start: MemberStateStopping,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateCheckerStarted,
				MemberStateErrored,
				MemberStateExcluded,
				MemberStateJoined,
				MemberStateReady,
				MemberStateStarting,
				MemberStateStopped,
				MemberStateUnresponsive,
				MemberStateUnknown,
			},
		},
		"stopped": {
			start: MemberStateStopped,
			expLegalStates: []MemberState{
				MemberStateAdminExcluded,
				MemberStateAwaitFormat,
				MemberStateCheckerStarted,
				MemberStateErrored,
				MemberStateExcluded,
				MemberStateJoined,
				MemberStateReady,
				MemberStateStarting,
				MemberStateStopping,
				MemberStateUnresponsive,
				MemberStateUnknown,
			},
		},
	} {
		expLegal := func(state MemberState) bool {
			for _, l := range tc.expLegalStates {
				if l == state {
					return true
				}
			}
			return false
		}

		for end := MemberStateUnknown; end < MemberStateMax; end = nextState(end) {
			caseName := fmt.Sprintf("%s/%s -> %s", name, tc.start, end)

			t.Run(caseName, func(t *testing.T) {
				result := tc.start.isTransitionIllegal(end)

				test.AssertEqual(t, !expLegal(end), result, "")
			})
		}
	}
}
