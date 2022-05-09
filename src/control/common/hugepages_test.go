//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package common

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common/test"
)

func TestCommon_getHugePageInfo(t *testing.T) {
	// Just a simple test to verify that we get something -- it should
	// pretty much never error.
	_, err := GetHugePageInfo()
	if err != nil {
		t.Fatal(err)
	}
}

func TestCommon_parseHugePageInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expOut    *HugePageInfo
		expFreeMB int
		expErr    error
	}{
		"none parsed": {
			expOut:    &HugePageInfo{},
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
			expOut: &HugePageInfo{
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
			expOut: &HugePageInfo{
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
			CmpErr(t, tc.expErr, gotErr)
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

func TestCommon_CalcMinHugePages(t *testing.T) {
	for name, tc := range map[string]struct {
		input      *HugePageInfo
		numTargets int
		expPages   int
		expErr     error
	}{
		"no pages": {
			input:      &HugePageInfo{},
			numTargets: 1,
			expErr:     errors.New("invalid system hugepage size"),
		},
		"no targets": {
			input:  &HugePageInfo{},
			expErr: errors.New("numTargets"),
		},
		"2KB pagesize; 16 targets": {
			input: &HugePageInfo{
				PageSizeKb: 2048,
			},
			numTargets: 16,
			expPages:   8192,
		},
		"2KB pagesize; 31 targets": {
			input: &HugePageInfo{
				PageSizeKb: 2048,
			},
			numTargets: 31,
			expPages:   15872,
		},
		"1GB pagesize; 16 targets": {
			input: &HugePageInfo{
				PageSizeKb: 1048576,
			},
			numTargets: 16,
			expPages:   16,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotPages, gotErr := CalcMinHugePages(tc.input.PageSizeKb, tc.numTargets)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if gotPages != tc.expPages {
				t.Fatalf("expected %d, got %d", tc.expPages, gotPages)
			}
		})
	}
}
