//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

func TestFlags_LabelOrUUIDFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *LabelOrUUIDFlag
		isEmpty   bool
		hasUUID   bool
		hasLabel  bool
		expString string
		expErr    error
	}{
		"unset": {
			expFlag:   &LabelOrUUIDFlag{},
			isEmpty:   true,
			expString: "<no label or uuid set>",
		},
		"valid UUID": {
			arg: "13167ad2-4479-4b88-9d45-13181c152974",
			expFlag: &LabelOrUUIDFlag{
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
			expFlag: &LabelOrUUIDFlag{
				Label: "13167ad2-4479-4b88-9d45-13181c15297",
			},
			hasLabel:  true,
			expString: "13167ad2-4479-4b88-9d45-13181c15297",
		},
		"valid label": {
			arg: "this-is-a-good-label",
			expFlag: &LabelOrUUIDFlag{
				Label: "this-is-a-good-label",
			},
			hasLabel:  true,
			expString: "this-is-a-good-label",
		},
		// TODO: Add more testcases when we have label validation rules in place.
	} {
		t.Run(name, func(t *testing.T) {
			f := LabelOrUUIDFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			common.AssertEqual(t, tc.isEmpty, f.Empty(), "unexpected Empty()")
			common.AssertEqual(t, tc.hasUUID, f.HasUUID(), "unexpected HasUUID()")
			common.AssertEqual(t, tc.hasLabel, f.HasLabel(), "unexpected HasLabel()")
			common.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestFlags_EpochRangeFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *EpochRangeFlag
		expString string
		expErr    error
	}{
		"unset": {
			expErr: errors.New("failed to parse"),
		},
		"non-numeric a": {
			arg:    "a-b",
			expErr: errors.New(`failed to parse "a"`),
		},
		"non-numeric b": {
			arg:    "1-b",
			expErr: errors.New(`failed to parse "b"`),
		},
		"not a range": {
			arg:    "1",
			expErr: errors.New(`failed to parse "1"`),
		},
		"too much range": {
			arg:    "1--2",
			expErr: errors.New(`failed to parse "1--2"`),
		},
		"way too much range": {
			arg:    "1-2-3",
			expErr: errors.New(`failed to parse "1-2-3"`),
		},
		"begin > end": {
			arg:    "2-1",
			expErr: errors.New("begin"),
		},
		"valid range": {
			arg: "1-2",
			expFlag: &EpochRangeFlag{
				Set:   true,
				Begin: 1,
				End:   2,
			},
			expString: "1-2",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := EpochRangeFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			common.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestFlags_ChunkSizeFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *ChunkSizeFlag
		expString string
		expErr    error
	}{
		"unset": {
			expErr: errors.New("empty chunk size"),
		},
		"bytes": {
			arg: "1048576",
			expFlag: &ChunkSizeFlag{
				Set:  true,
				Size: 1048576,
			},
			expString: "1.0 MiB",
		},
		"MB": {
			arg: "1MB",
			expFlag: &ChunkSizeFlag{
				Set:  true,
				Size: 1000000,
			},
			expString: "977 KiB",
		},
		"MiB": {
			arg: "1MiB",
			expFlag: &ChunkSizeFlag{
				Set:  true,
				Size: 1048576,
			},
			expString: "1.0 MiB",
		},
		"not a size": {
			arg:    "snausages",
			expErr: errors.New("ParseFloat"),
		},
		// TODO: More validation of allowed sizes?
	} {
		t.Run(name, func(t *testing.T) {
			f := ChunkSizeFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			common.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestFlags_OidFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *OidFlag
		expString string
		expErr    error
	}{
		"unset": {
			expErr: errors.New("empty oid"),
		},
		"non-numeric a": {
			arg:    "a.b",
			expErr: errors.New(`parsing "a"`),
		},
		"non-numeric b": {
			arg:    "1.b",
			expErr: errors.New(`parsing "b"`),
		},
		"not an oid": {
			arg:    "1",
			expErr: errors.New(`failed to parse "1"`),
		},
		"too much oid": {
			arg:    "1..2",
			expErr: errors.New(`failed to parse "1..2"`),
		},
		"way too much oid": {
			arg:    "1.2.3",
			expErr: errors.New(`failed to parse "1.2.3"`),
		},
		"negative oid": {
			arg:    "-1152922363600306176.0",
			expErr: errors.New(`failed to parse`),
		},
		"valid oid": {
			arg: "1152922363600306176.0",
			expFlag: &OidFlag{
				Set: true,
				Oid: makeOid(1152922363600306176, 0),
			},
			expString: "1152922363600306176.0",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := OidFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			common.AssertEqual(t, tc.expString, f.String(), "unexpected String()")
		})
	}
}

func TestFlags_ObjClassFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *ObjClassFlag
		expString string
		expErr    error
	}{
		"unset": {
			expErr: errors.New("empty object class"),
		},
		"invalid": {
			arg:    "snausages",
			expErr: errors.New("unknown object class"),
		},
		"valid": {
			arg: "S2",
			expFlag: &ObjClassFlag{
				Set:   true,
				Class: 201,
			},
			expString: "S2",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ObjClassFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			flagTestFini, err := flagTestInit()
			if err != nil {
				t.Fatal(err)
			}
			defer flagTestFini()

			common.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestFlags_ConsModeFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *ConsModeFlag
		expString string
		expErr    error
	}{
		"unset": {
			expErr: errors.New("empty cons mode"),
		},
		"invalid": {
			arg:    "snausages",
			expErr: errors.New("unknown consistency mode"),
		},
		"relaxed": {
			arg: "relaxed",
			expFlag: &ConsModeFlag{
				Set:  true,
				Mode: 0,
			},
			expString: "relaxed",
		},
		"balanced": {
			arg: "balanced",
			expFlag: &ConsModeFlag{
				Set:  true,
				Mode: 4,
			},
			expString: "balanced",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ConsModeFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			common.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}
