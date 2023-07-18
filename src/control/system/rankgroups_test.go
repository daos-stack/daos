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
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

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
				"foo": ranklist.MustCreateRankSet("0-4"),
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
			expErr: &ErrMemberExists{Rank: ranklist.NewRankPtr(4)},
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
				"foo": ranklist.MustCreateRankSet("0-4"),
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
			expErr: &ErrMemberExists{Rank: ranklist.NewRankPtr(4)},
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
