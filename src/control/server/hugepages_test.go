//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package server

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

func TestServer_getHugePageInfo(t *testing.T) {
	// Just a simple test to verify that we get something -- it should
	// pretty much never error.
	_, err := getHugePageInfo()
	if err != nil {
		t.Fatal(err)
	}
}

func TestServer_parseHugePageInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expOut    *hugePageInfo
		expFreeMB int
		expErr    error
	}{
		"none parsed": {
			expOut:    &hugePageInfo{},
			expFreeMB: 0,
		},
		"2MB pagesize": {
			input: `
HugePages_Total:    1024
HugePages_Free:     1023
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
			`,
			expOut: &hugePageInfo{
				Total:      1024,
				Free:       1023,
				PageSizeKb: 2048,
			},
			expFreeMB: 2046,
		},
		"1GB pagesize": {
			input: `
HugePages_Total:      16
HugePages_Free:       16
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       1048576 kB
			`,
			expOut: &hugePageInfo{
				Total:      16,
				Free:       16,
				PageSizeKb: 1048576,
			},
			expFreeMB: 16384,
		},
		"weird pagesize": {
			input: `
Hugepagesize:       blerble 1 GB
			`,
			expErr: errors.New("unable to parse"),
		},
		"weird pagesize unit": {
			input: `
Hugepagesize:       1 GB
			`,
			expErr: errors.New("unhandled page size"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			rdr := strings.NewReader(tc.input)

			gotOut, gotErr := parseHugePageInfo(rdr)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOut, gotOut); diff != "" {
				t.Fatalf("unexpected output (-want, +got)\n%s\n", diff)
			}

			if gotOut.FreeMB() != tc.expFreeMB {
				t.Fatalf("expected FreeMB() to be %d, got %d",
					tc.expFreeMB, gotOut.FreeMB())
			}
		})
	}
}
