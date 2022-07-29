//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package daos_test

import (
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestDrpc_LabelIsValid(t *testing.T) {
	// Not intended to be exhaustive. Just some basic smoke tests
	// to verify that we can call the C function and get sensible
	// results.
	for name, tc := range map[string]struct {
		label     string
		expResult bool
	}{
		"zero-length fails": {"", false},
		"overlength fails":  {strings.Repeat("x", daos.MaxLabelLength+1), false},
		"uuid fails":        {"54f26bfd-628f-4762-a28a-1c42bcb6565b", false},
		"max-length ok":     {strings.Repeat("x", daos.MaxLabelLength), true},
		"valid chars ok":    {"this:is_a_valid-label.", true},
	} {
		t.Run(name, func(t *testing.T) {
			gotResult := daos.LabelIsValid(tc.label)
			test.AssertEqual(t, tc.expResult, gotResult, "unexpected label check result")
		})
	}
}
