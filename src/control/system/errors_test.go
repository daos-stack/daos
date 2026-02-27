//
// (C) Copyright 2024 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

func TestSystem_Errors_IsNotReady(t *testing.T) {
	for name, tc := range map[string]struct {
		err       error
		expResult bool
	}{
		"nil": {},
		"uninitialized": {
			err:       ErrUninitialized,
			expResult: true,
		},
		"raft not available": {
			err:       ErrRaftUnavail,
			expResult: true,
		},
		"leadership transfer in progress": {
			err:       ErrLeaderStepUpInProgress,
			expResult: true,
		},
		"something else": {
			err: errors.New("something is wrong"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, IsNotReady(tc.err), "")
		})
	}
}

func TestSystem_Errors_IsUninitialized(t *testing.T) {
	for name, tc := range map[string]struct {
		err       error
		expResult bool
	}{
		"nil": {},
		"uninitialized": {
			err:       ErrUninitialized,
			expResult: true,
		},
		"unavailable not uninitialized": {
			err: ErrRaftUnavail,
		},
		"something else": {
			err: errors.New("something is wrong"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, IsUninitialized(tc.err), "")
		})
	}
}

func TestSystem_Errors_IsUnavailable(t *testing.T) {
	for name, tc := range map[string]struct {
		err       error
		expResult bool
	}{
		"nil": {},
		"raft not available": {
			err:       ErrRaftUnavail,
			expResult: true,
		},
		"leadership transfer in progress": {
			err:       ErrLeaderStepUpInProgress,
			expResult: true,
		},
		"data plane not started": {
			err:       &fault.Fault{Code: code.ServerDataPlaneNotStarted},
			expResult: true,
		},
		"wrapped data plane not started": {
			err:       errors.Wrap(&fault.Fault{Code: code.ServerDataPlaneNotStarted}, "wrapped error"),
			expResult: true,
		},
		"uninitialized not unavailable": {
			err: ErrUninitialized,
		},
		"something else": {
			err: errors.New("something is wrong"),
		},
		"member exists not unavailable": {
			err: ErrRankExists(1),
		},
		"member not found not unavailable": {
			err: ErrMemberRankNotFound(1),
		},
		"pool not found not unavailable": {
			err: ErrPoolRankNotFound(1),
		},
		"different fault code not unavailable": {
			err: &fault.Fault{Code: code.ClientUnknown},
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, IsUnavailable(tc.err), "")
		})
	}
}
