//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestFlags_EpochFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *EpochFlag
		expString string
		expErr    error
	}{
		"unset": {
			expErr: errors.New("failed to parse"),
		},
		"invalid input non-numeric": {
			arg:    "xyzt123",
			expErr: errors.New(`failed to parse "xyzt123"`),
		},
		"invalid value hex without prefix": {
			arg:    "51b0717086c0000",
			expErr: errors.New(`failed to parse "51b0717086c0000"`),
		},
		"invalid value negative": {
			arg:    "-1",
			expErr: errors.New(`failed to parse "-1"`),
		},
		"invalid value larger than max uint64": {
			arg:    "18446744073709551616",
			expErr: errors.New(`failed to parse "18446744073709551616"`),
		},
		"invalid value hex larger than max uint64": {
			arg:    "0x10000000000000000",
			expErr: errors.New(`failed to parse "0x10000000000000000"`),
		},
		"valid value hex with prefix": {
			arg: "0x71b0717086c0000",
			expFlag: &EpochFlag{
				Set:   true,
				Value: 512010778143621120,
			},
			expString: "0x71b0717086c0000",
		},
		"valid value": {
			arg: "512010778143621120",
			expFlag: &EpochFlag{
				Set:   true,
				Value: 512010778143621120,
			},
			expString: "0x71b0717086c0000",
		},
		"valid value large hex": {
			arg: "0xfffffffffffffffe",
			expFlag: &EpochFlag{
				Set:   true,
				Value: 18446744073709551614,
			},
			expString: "0xfffffffffffffffe",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := EpochFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

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
		"not a range single value hex": {
			arg:    "0x1",
			expErr: errors.New(`failed to parse "0x1"`),
		},
		"number too large for uint64": {
			arg:    "0x71b0717086c00f0-18446744073709551616",
			expErr: errors.New(`failed to parse "18446744073709551616"`),
		},
		"number in hex too large for uint64": {
			arg:    "0x71b0717086c00f0-0x10000000000000000",
			expErr: errors.New(`failed to parse "0x10000000000000000"`),
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
		"begin > end hex values": {
			arg:    "0x71b0717086c00f0-0x71b0717086c0000",
			expErr: errors.New("begin"),
		},
		"valid range": {
			arg: "1-2",
			expFlag: &EpochRangeFlag{
				Set:   true,
				Begin: 1,
				End:   2,
			},
			expString: "0x1-0x2",
		},
		"valid range hex values": {
			arg: "0x71b0717086c0000-0x71b0717086c00f0",
			expFlag: &EpochRangeFlag{
				Set:   true,
				Begin: 512010778143621120,
				End:   512010778143621360,
			},
			expString: "0x71b0717086c0000-0x71b0717086c00f0",
		},
		"valid range first value hex": {
			arg: "0x71b0717086c0000-512010778143621360",
			expFlag: &EpochRangeFlag{
				Set:   true,
				Begin: 512010778143621120,
				End:   512010778143621360,
			},
			expString: "0x71b0717086c0000-0x71b0717086c00f0",
		},
		"valid range second value hex": {
			arg: "512010778143621120-0x71b0717086c00f0",
			expFlag: &EpochRangeFlag{
				Set:   true,
				Begin: 512010778143621120,
				End:   512010778143621360,
			},
			expString: "0x71b0717086c0000-0x71b0717086c00f0",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := EpochRangeFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

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
			expErr: errors.New("invalid chunk-size \"snausages\""),
		},
		// TODO: More validation of allowed sizes?
	} {
		t.Run(name, func(t *testing.T) {
			f := ChunkSizeFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

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
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")
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
				Class: 16777218,
			},
			expString: "S2",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ObjClassFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			flagTestFini, err := flagTestInit()
			if err != nil {
				t.Fatal(err)
			}
			defer flagTestFini()

			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

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
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestFlags_ContTypeFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg       string
		expFlag   *ContTypeFlag
		expString string
		expErr    error
	}{
		"unset": {
			expErr: errors.New("empty container type"),
		},
		"invalid": {
			arg:    "snausages",
			expErr: errors.New("unknown container type"),
		},
		"valid": {
			arg: "pOsIx",
			expFlag: &ContTypeFlag{
				Set:  true,
				Type: 1,
			},
			expString: "POSIX",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ContTypeFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			flagTestFini, err := flagTestInit()
			if err != nil {
				t.Fatal(err)
			}
			defer flagTestFini()

			test.AssertEqual(t, tc.expString, f.String(), "unexpected String()")

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestFlags_FsCheckFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg     string
		expFlag *FsCheckFlag
		expErr  error
	}{
		"unset": {
			expErr: errors.New("empty filesystem check flags"),
		},
		"invalid": {
			arg:    "unlink",
			expErr: errors.New("unknown filesystem check flags: \"unlink\""),
		},
		"valid1": {
			arg: "print",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 1,
			},
		},
		"valid2": {
			arg: "remove",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 2,
			},
		},
		"valid3": {
			arg: "print,remove",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 3,
			},
		},
		"valid4": {
			arg: "relink",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 4,
			},
		},
		"valid5": {
			arg: "print,relink",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 5,
			},
		},
		"valid8": {
			arg: "verify",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 8,
			},
		},
		"valid9": {
			arg: "print,verify",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 9,
			},
		},
		"valid11": {
			arg: "print,verify,remove",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 11,
			},
		},
		"valid13": {
			arg: "print,verify,relink",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 13,
			},
		},
		"valid16": {
			arg: "evict",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 16,
			},
		},
		"valid29": {
			arg: "print,verify,relink,evict",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 29,
			},
		},
		"untrimmed, upper case": {
			arg: "print, VeRiFy,relink",
			expFlag: &FsCheckFlag{
				Set:   true,
				Flags: 13,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := FsCheckFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			flagTestFini, err := flagTestInit()
			if err != nil {
				t.Fatal(err)
			}
			defer flagTestFini()

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestFlags_ModeBitsFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		arg     string
		expFlag *ModeBitsFlag
		expErr  error
	}{
		"unset": {
			expErr: errors.New("empty file mode flag"),
		},
		"not an integer": {
			arg:    "foo",
			expErr: errors.New("invalid mode: \"foo\""),
		},
		"invalid octal": {
			arg:    "0999",
			expErr: errors.New("invalid mode: \"0999\""),
		},
		"negative value": {
			arg:    "-0755",
			expErr: errors.New("invalid mode: \"-0755\""),
		},
		"valid": {
			arg: "0755",
			expFlag: &ModeBitsFlag{
				Set:  true,
				Mode: 0755,
			},
		},
		"valid without leading zero": {
			arg: "755",
			expFlag: &ModeBitsFlag{
				Set:  true,
				Mode: 0755,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ModeBitsFlag{}
			gotErr := f.UnmarshalFlag(tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expFlag, &f); diff != "" {
				t.Fatalf("unexpected flag value: (-want, +got)\n%s\n", diff)
			}
		})
	}

}
