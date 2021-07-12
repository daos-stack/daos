//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package drpc_test

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
)

func TestDrpc_LabelIsValid(t *testing.T) {
	// Not intended to be exhaustive. Just some basic smoke tests
	// to verify that we can call the C function and get sensible
	// results.
	for name, tc := range map[string]struct {
		label     string
		expResult bool
	}{
		"zero-length": {"", false},
		"overlength":  {"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABBBBBBBBBBBBBBBBBBBBBBBBBBBBBBbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb00000000000000000000000000000000000000000------------------------------------ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", false},
		"valid":       {"this:is_a_valid_label.", true},
	} {
		t.Run(name, func(t *testing.T) {
			gotResult := drpc.LabelIsValid(tc.label)
			common.AssertEqual(t, tc.expResult, gotResult, "unexpected label check result")
		})
	}
}
