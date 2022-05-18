//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui_test

import (
	"errors"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

func TestFlags_LabelOrUUIDFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *ui.LabelOrUUIDFlag
		isEmpty   bool
		hasUUID   bool
		hasLabel  bool
		expString string
		expErr    error
	}{
		"unset": {
			expErr: errors.New("invalid label"),
		},
		"valid UUID": {
			arg: "13167ad2-4479-4b88-9d45-13181c152974",
			expFlag: &ui.LabelOrUUIDFlag{
				UUID: uuid.MustParse("13167ad2-4479-4b88-9d45-13181c152974"),
			},
			hasUUID:   true,
			expString: "13167ad2-4479-4b88-9d45-13181c152974",
		},
		"valid label": {
			arg: "this:is_a_good-label.",
			expFlag: &ui.LabelOrUUIDFlag{
				Label: "this:is_a_good-label.",
			},
			hasLabel:  true,
			expString: "this:is_a_good-label.",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.LabelOrUUIDFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.isEmpty, f.Empty(), "unexpected Empty()")
			test.AssertEqual(t, tc.hasUUID, f.HasUUID(), "unexpected HasUUID()")
			test.AssertEqual(t, tc.hasLabel, f.HasLabel(), "unexpected HasLabel()")
			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f, cmpopts.IgnoreUnexported(ui.LabelOrUUIDFlag{})); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}
