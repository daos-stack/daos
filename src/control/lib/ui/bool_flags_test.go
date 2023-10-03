//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestUI_EnabledFlag(t *testing.T) {
	trueVals := []string{"true", "1", "yes", "on"}
	falseVals := []string{"false", "0", "no", "off"}

	for _, val := range trueVals {
		testFlag := EnabledFlag{}
		t.Run(val, func(t *testing.T) {
			test.CmpErr(t, nil, testFlag.UnmarshalFlag(val))
		})
		test.AssertTrue(t, testFlag.Set, "not set")
		test.AssertTrue(t, testFlag.Enabled, "not enabled")
	}
	for _, val := range falseVals {
		testFlag := EnabledFlag{}
		t.Run(val, func(t *testing.T) {
			test.CmpErr(t, nil, testFlag.UnmarshalFlag(val))
		})
		test.AssertTrue(t, testFlag.Set, "not set")
		test.AssertFalse(t, testFlag.Enabled, "enabled")
	}

	testFlag := EnabledFlag{}
	if err := testFlag.UnmarshalFlag("invalid"); err == nil {
		t.Fatal("expected error")
	}
}
