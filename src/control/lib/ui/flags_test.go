//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui_test

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

type customID struct {
	ui.LabelOrUUIDFlag
	fn func(string) bool
}

func (f *customID) UnmarshalFlag(fv string) error {
	f.SetLabelValidator(f.fn)
	return f.SetLabel(fv)
}

func TestFlags_LabelOrUUIDFlag_customLabelValidation(t *testing.T) {
	for name, tc := range map[string]struct {
		arg    string
		fn     func(string) bool
		expErr error
	}{
		"everything's ok": {
			fn: func(_ string) bool { return true },
		},
		"no a's allowed": {
			arg: "a-ok",
			fn: func(l string) bool {
				return !strings.Contains(l, "a")
			},
			expErr: errors.New("invalid label"),
		},
		"default disallows empty": {
			expErr: errors.New("invalid label"),
		},
		"default allows non-empty": {
			arg: "not empty",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := customID{fn: tc.fn}
			gotErr := f.UnmarshalFlag(tc.arg)

			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

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
		"valid UUID": {
			arg: "13167ad2-4479-4b88-9d45-13181c152974",
			expFlag: &ui.LabelOrUUIDFlag{
				UUID: uuid.MustParse("13167ad2-4479-4b88-9d45-13181c152974"),
			},
			hasUUID:   true,
			expString: "13167ad2-4479-4b88-9d45-13181c152974",
		},
		// This seems kind of dodgy... A malformed UUID will be interpreted as
		// a label. The problem is that it gets tricky to reliably determine when
		// something is "UUID-ish" enough to call it a malformed UUID instead of
		// treating it as a label.
		"invalid UUID": {
			arg: "13167ad2-4479-4b88-9d45-13181c15297",
			expFlag: &ui.LabelOrUUIDFlag{
				Label: "13167ad2-4479-4b88-9d45-13181c15297",
			},
			hasLabel:  true,
			expString: "13167ad2-4479-4b88-9d45-13181c15297",
		},
		"valid label": {
			arg: "this-is-a-good-label",
			expFlag: &ui.LabelOrUUIDFlag{
				Label: "this-is-a-good-label",
			},
			hasLabel:  true,
			expString: "this-is-a-good-label",
		},
		"unset": {
			expErr: errors.New("invalid label"),
		},
		// NB: Real label validation will be done via a DAOS API call,
		// in order to centralize the logic across implementations. By
		// default, this type just cares that the label is not empty.
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.LabelOrUUIDFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			common.AssertEqual(t, tc.isEmpty, f.Empty(), "unexpected Empty()")
			common.AssertEqual(t, tc.hasUUID, f.HasUUID(), "unexpected HasUUID()")
			common.AssertEqual(t, tc.hasLabel, f.HasLabel(), "unexpected HasLabel()")
			common.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f, cmpopts.IgnoreUnexported(ui.LabelOrUUIDFlag{})); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}
