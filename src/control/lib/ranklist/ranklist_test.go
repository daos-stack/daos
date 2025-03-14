//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ranklist

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestRanklist_RankSet(t *testing.T) {
	for name, tc := range map[string]struct {
		ranks    string
		addRanks []Rank
		expOut   string
		expCount int
		expRanks []Rank
		expErr   error
	}{
		"empty start list": {
			ranks:    "",
			expOut:   "",
			expCount: 0,
			expRanks: []Rank{},
		},
		"invalid with hostnames": {
			ranks:  "node2-1,node1-2.suffix1,node1-[45,47].suffix2,node3,node1-3",
			expErr: errors.New("unexpected alphabetic character(s)"),
		},
		"simple ranged rank list": {
			ranks:    "0-10",
			expOut:   "0-10",
			expCount: 11,
			expRanks: []Rank{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
		},
		"deranged rank list": {
			ranks:    "1,2,3,5,6,8,10,10,1",
			expOut:   "1-3,5-6,8,10",
			expCount: 7,
			expRanks: []Rank{1, 2, 3, 5, 6, 8, 10},
		},
		"ranged rank list": {
			ranks:    "2,3,10-19,0-9",
			addRanks: []Rank{30, 32},
			expOut:   "0-19,30,32",
			expCount: 22,
			expRanks: []Rank{
				0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
				10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
				30, 32,
			},
		},
		"adding to empty list": {
			ranks:    "",
			addRanks: []Rank{30, 32},
			expOut:   "30,32",
			expCount: 2,
			expRanks: []Rank{30, 32},
		},
		"invalid list with spaces": {
			ranks:  "1, 5",
			expErr: errors.New("unexpected whitespace character(s)"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			rs, gotErr := CreateRankSet(tc.ranks)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			for _, r := range tc.addRanks {
				rs.Add(r)
			}

			if diff := cmp.Diff(tc.expOut, rs.String()); diff != "" {
				t.Fatalf("unexpected value (-want, +got):\n%s\n", diff)
			}

			gotCount := rs.Count()
			if gotCount != tc.expCount {
				t.Fatalf("\nexpected count to be %d; got %d", tc.expCount, gotCount)
			}

			ranks := rs.Ranks()
			if diff := cmp.Diff(tc.expRanks, ranks); diff != "" {
				t.Fatalf("unexpected ranks (-want, +got):\n%s\n", diff)
			}

		})
	}
}

func TestRanklist_RankSet_Contains(t *testing.T) {
	for name, tc := range map[string]struct {
		ranks       string
		searchRank  Rank
		expContains bool
	}{
		"missing": {
			ranks:       "1-128",
			searchRank:  200,
			expContains: false,
		},
		"found": {
			ranks:       "1-128",
			searchRank:  126,
			expContains: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			rs, err := CreateRankSet(tc.ranks)
			if err != nil {
				t.Fatal(err)
			}

			gotContains := rs.Contains(tc.searchRank)
			if gotContains != tc.expContains {
				t.Fatalf("expected %d to be found in %s, want %v got %v",
					tc.searchRank, tc.ranks, tc.expContains, gotContains)
			}
		})
	}
}

func TestRanklist_RankSet_MarshalJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		rankSet *RankSet
		expOut  string
		expErr  error
	}{
		"nil rankset": {
			expOut: "null",
		},
		"empty rankset": {
			rankSet: &RankSet{},
			expOut:  "[]",
		},
		"simple rankset": {
			rankSet: MustCreateRankSet("[0-3]"),
			expOut:  "[0,1,2,3]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotBytes, gotErr := tc.rankSet.MarshalJSON()
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOut, string(gotBytes)); diff != "" {
				t.Fatalf("unexpected value (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestRanklist_RankSet_UnmarshalJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		dataStr string
		rankSet *RankSet
		expSet  *RankSet
		expErr  error
	}{
		"nil rankset": {
			dataStr: "[0,1,2,3]",
			expErr:  errors.New("nil RankSet"),
		},
		"invalid range string": {
			dataStr: "3-0",
			rankSet: &RankSet{},
			expErr:  errors.New("invalid range"),
		},
		"empty data": {
			rankSet: &RankSet{},
			expSet:  &RankSet{},
		},
		"empty slice": {
			dataStr: "[]",
			rankSet: &RankSet{},
			expSet:  &RankSet{},
		},
		"empty slice, quoted": {
			dataStr: "\"[]\"",
			rankSet: &RankSet{},
			expSet:  &RankSet{},
		},
		"rankset from ranks": {
			rankSet: &RankSet{},
			dataStr: "[0,1,2,3,5]",
			expSet:  MustCreateRankSet("[0-3,5]"),
		},
		"rankset from range": {
			rankSet: &RankSet{},
			dataStr: "[0-3,5]",
			expSet:  MustCreateRankSet("[0-3,5]"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := tc.rankSet.UnmarshalJSON([]byte(tc.dataStr))
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := cmp.Options{
				cmp.Comparer(func(x, y *RankSet) bool {
					return x.String() == y.String()
				}),
			}
			if diff := cmp.Diff(tc.expSet, tc.rankSet, cmpOpts...); diff != "" {
				t.Fatalf("unexpected RankSet (-want, +got):\n%s\n", diff)
			}
		})
	}
}
