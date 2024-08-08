//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui_test

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

func TestUI_FmtHumanSize(t *testing.T) {
	for name, tc := range map[string]struct {
		input   float64
		suffix  string
		binary  bool
		expSize string
	}{
		"0": {
			input:   0,
			suffix:  "B",
			expSize: "0 B",
		},
		"-0": {
			input:   -0,
			suffix:  "B",
			expSize: "0 B",
		},
		"-1": {
			input:   -1,
			suffix:  "B",
			expSize: "-1.00 B",
		},
		"1 no suffix": {
			input:   1,
			expSize: "1.00",
		},
		"1 binary no suffix": {
			input:   1,
			binary:  true,
			expSize: "1.00",
		},
		"1000 no suffix": {
			input:   1000,
			expSize: "1.00 K",
		},
		"1000 binary no suffix": {
			input:   1000,
			binary:  true,
			expSize: "1000.00",
		},
		"1024 binary no suffix": {
			input:   1024,
			binary:  true,
			expSize: "1.00 K",
		},
		"4.5PB": {
			input:   1 << 52,
			suffix:  "B",
			expSize: "4.50 PB",
		},
		"4PiB binary": {
			input:   1 << 52,
			suffix:  "B",
			binary:  true,
			expSize: "4.00 PiB",
		},
		"trouble": {
			input:   1 << 90,
			suffix:  "tribbles",
			expSize: "1.237940E+27 tribbles",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotSize := ui.FmtHumanSize(tc.input, tc.suffix, tc.binary)
			test.AssertEqual(t, tc.expSize, gotSize, "unexpected size")
		})
	}
}

func TestUI_ByteSizeFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		input   string
		expSize uint64
		expStr  string
		expErr  error
	}{
		"empty": {
			expErr: errors.New("no size specified"),
		},
		"invalid size": {
			input:  "horse",
			expErr: errors.New("invalid size"),
		},
		"negative size invalid": {
			input:  "-438 TB",
			expErr: errors.New("invalid size"),
		},
		"0": {
			input:   "0",
			expSize: 0,
			expStr:  "0 B",
		},
		"weird but valid": {
			input:   "0 EiB",
			expSize: 0,
			expStr:  "0 B",
		},
		"valid MB": {
			input:   "10MB",
			expSize: 10 * 1000 * 1000,
			expStr:  "10 MB",
		},
		"valid raw number": {
			input:   "1058577",
			expSize: 1058577,
			expStr:  "1.1 MB",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := ui.ByteSizeFlag{}
			gotErr := f.UnmarshalFlag(tc.input)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				test.AssertFalse(t, f.IsSet(), "shouldn't be set on error")
				return
			}
			test.AssertTrue(t, f.IsSet(), "should be set on success")
			test.AssertEqual(t, tc.expSize, f.Bytes, "unexpected size")
			test.AssertEqual(t, tc.expStr, f.String(), "unexpected string")
		})
	}
}
