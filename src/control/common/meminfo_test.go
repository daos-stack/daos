//
// (C) Copyright 2019-2023 Intel Corporation.
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

func TestCommon_getMemInfo(t *testing.T) {
	// Just a simple test to verify that we get something -- it should
	// pretty much never error.
	_, err := GetMemInfo()
	if err != nil {
		t.Fatal(err)
	}
}

func TestCommon_parseMemInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expOut    *MemInfo
		expFreeMB int
		expErr    error
	}{
		"none parsed": {
			expOut:    &MemInfo{},
			expFreeMB: 0,
		},
		"2MB pagesize": {
			input: `
MemTotal:           1024 kB
MemFree:            1024 kB
MemAvailable:       1024 kB
HugePages_Total:    1024
HugePages_Free:     1023
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
			`,
			expOut: &MemInfo{
				HugepagesTotal:  1024,
				HugepagesFree:   1023,
				HugepageSizeKiB: 2048,
				MemTotalKiB:     1024,
				MemFreeKiB:      1024,
				MemAvailableKiB: 1024,
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
			expOut: &MemInfo{
				HugepagesTotal:  16,
				HugepagesFree:   16,
				HugepageSizeKiB: 1048576,
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
			expErr: errors.New("unhandled size unit \"GB\""),
		},
	} {
		t.Run(name, func(t *testing.T) {
			rdr := strings.NewReader(tc.input)

			gotOut, gotErr := parseMemInfo(rdr)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOut, gotOut); diff != "" {
				t.Fatalf("unexpected output (-want, +got)\n%s\n", diff)
			}

			if gotOut.HugepagesFreeMB() != tc.expFreeMB {
				t.Fatalf("expected FreeMB() to be %d, got %d",
					tc.expFreeMB, gotOut.HugepagesFreeMB())
			}
		})
	}
}
