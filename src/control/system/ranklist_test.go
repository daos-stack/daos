//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestSystem_RankSet(t *testing.T) {
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

func TestSystem_RankGroupsFromMembers(t *testing.T) {
	for name, tc := range map[string]struct {
		rankGroups RankGroups
		members    Members
		expStates  []string
		expOut     string
		expErr     error
	}{
		"nil groups": {
			expErr: errors.New("expecting non-nil"),
		},
		"existing groups": {
			rankGroups: RankGroups{
				"foo": MustCreateRankSet("0-4"),
			},
			expErr: errors.New("expecting non-nil empty"),
		},
		"nil members": {
			rankGroups: make(RankGroups),
			expStates:  []string{},
		},
		"no members": {
			rankGroups: make(RankGroups),
			members:    Members{},
			expStates:  []string{},
		},
		"multiple groups with duplicate": {
			rankGroups: make(RankGroups),
			members: Members{
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateExcluded),
				MockMember(t, 4, MemberStateExcluded),
				MockMember(t, 4, MemberStateExcluded),
				MockMember(t, 1, MemberStateJoined),
			},
			expErr: &ErrMemberExists{Rank: NewRankPtr(4)},
		},
		"multiple groups": {
			rankGroups: make(RankGroups),
			members: Members{
				MockMember(t, 1, MemberStateStopped),
				MockMember(t, 3, MemberStateJoined),
				MockMember(t, 5, MemberStateJoined),
				MockMember(t, 4, MemberStateJoined),
				MockMember(t, 8, MemberStateJoined),
				MockMember(t, 2, MemberStateExcluded),
			},
			expStates: []string{
				MemberStateJoined.String(),
				MemberStateExcluded.String(),
				MemberStateStopped.String(),
			},
			expOut: "3-5,8: Joined\n    2: Excluded\n    1: Stopped\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := tc.rankGroups.FromMembers(tc.members)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expStates, tc.rankGroups.Keys()); diff != "" {
				t.Fatalf("unexpected states (-want, +got):\n%s\n", diff)
			}

			if diff := cmp.Diff(tc.expOut, tc.rankGroups.String()); diff != "" {
				t.Fatalf("unexpected repr (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_RankGroupsFromMemberResults(t *testing.T) {
	for name, tc := range map[string]struct {
		rankGroups RankGroups
		results    MemberResults
		expGroups  []string
		expOut     string
		expErr     error
	}{
		"nil groups": {
			expErr: errors.New("expecting non-nil"),
		},
		"existing groups": {
			rankGroups: RankGroups{
				"foo": MustCreateRankSet("0-4"),
			},
			expErr: errors.New("expecting non-nil empty"),
		},
		"nil results": {
			rankGroups: make(RankGroups),
			expGroups:  []string{},
		},
		"no results": {
			rankGroups: make(RankGroups),
			expGroups:  []string{},
			results:    MemberResults{},
		},
		"results with no action": {
			rankGroups: make(RankGroups),
			results: MemberResults{
				MockMemberResult(4, "ping", nil, MemberStateExcluded),
				MockMemberResult(5, "", nil, MemberStateExcluded),
			},
			expErr: errors.New("action field empty for rank 5 result"),
		},
		"results with duplicate ranks": {
			rankGroups: make(RankGroups),
			results: MemberResults{
				MockMemberResult(4, "ping", nil, MemberStateExcluded),
				MockMemberResult(4, "ping", nil, MemberStateExcluded),
			},
			expErr: &ErrMemberExists{Rank: NewRankPtr(4)},
		},
		"successful results": {
			rankGroups: make(RankGroups),
			results: MemberResults{
				MockMemberResult(2, "ping", nil, MemberStateStopped),
				MockMemberResult(3, "ping", nil, MemberStateExcluded),
				MockMemberResult(4, "ping", nil, MemberStateExcluded),
				MockMemberResult(1, "ping", nil, MemberStateJoined),
			},
			expGroups: []string{"ping/OK"},
			expOut:    "1-4: ping/OK\n",
		},
		"mixed results": {
			rankGroups: make(RankGroups),
			results: MemberResults{
				MockMemberResult(2, "ping", nil, MemberStateStopped),
				MockMemberResult(3, "ping", nil, MemberStateExcluded),
				MockMemberResult(4, "ping", errors.New("failure 2"), MemberStateExcluded),
				MockMemberResult(5, "ping", errors.New("failure 2"), MemberStateExcluded),
				MockMemberResult(7, "ping", errors.New("failure 1"), MemberStateExcluded),
				MockMemberResult(6, "ping", errors.New("failure 1"), MemberStateExcluded),
				MockMemberResult(1, "ping", nil, MemberStateJoined),
			},
			expGroups: []string{"ping/OK", "ping/failure 1", "ping/failure 2"},
			expOut:    "1-3: ping/OK\n6-7: ping/failure 1\n4-5: ping/failure 2\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := tc.rankGroups.FromMemberResults(tc.results, "/")
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expGroups, tc.rankGroups.Keys()); diff != "" {
				t.Fatalf("unexpected states (-want, +got):\n%s\n", diff)
			}

			if diff := cmp.Diff(tc.expOut, tc.rankGroups.String()); diff != "" {
				t.Fatalf("unexpected repr (-want, +got):\n%s\n", diff)
			}
		})
	}
}
