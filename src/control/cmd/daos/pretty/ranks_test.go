//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty_test

import (
	"testing"

	"github.com/daos-stack/daos/src/control/cmd/daos/pretty"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

func TestPretty_PrintRanks_Uint32(t *testing.T) {
	for name, tc := range map[string]struct {
		ranks     []uint32
		expString string
	}{
		"nil": {},
		"empty": {
			ranks: []uint32{},
		},
		"ranks": {
			ranks:     []uint32{0, 1, 2, 3},
			expString: "[0-3]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotString := pretty.PrintRanks(tc.ranks)
			if gotString != tc.expString {
				t.Fatalf("got %q, want %q", gotString, tc.expString)
			}
		})
	}
}

func TestPretty_PrintRanks_RanklistRank(t *testing.T) {
	for name, tc := range map[string]struct {
		ranks     []ranklist.Rank
		expString string
	}{
		"nil": {},
		"empty": {
			ranks: []ranklist.Rank{},
		},
		"ranks": {
			ranks:     []ranklist.Rank{0, 1, 2, 3},
			expString: "[0-3]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotString := pretty.PrintRanks(tc.ranks)
			if gotString != tc.expString {
				t.Fatalf("got %q, want %q", gotString, tc.expString)
			}
		})
	}
}
