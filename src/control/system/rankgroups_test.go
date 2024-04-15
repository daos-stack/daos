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
		nilRankGroups    bool
		rankGroups       RankGroups
		results          MemberResults
		matchingFieldSep bool
		expGroups        []string
		expOut           string
		expErr           error
	}{
		"nil groups": {
			nilRankGroups: true,
			expErr:        errors.New("expecting non-nil"),
		},
		"existing groups": {
			rankGroups: RankGroups{
				"foo": ranklist.MustCreateRankSet("0-4"),
			},
			expErr: errors.New("expecting non-nil empty"),
		},
		"nil results": {
			expGroups: []string{},
		},
		"no results": {
			expGroups: []string{},
			results:   MemberResults{},
		},
		"results with no action": {
			results: MemberResults{
				MockMemberResult(4, "ping", nil, MemberStateExcluded),
				MockMemberResult(5, "", nil, MemberStateExcluded),
			},
			expErr: errors.New("action field empty for rank 5 result"),
		},
		"results with duplicate ranks": {
			results: MemberResults{
				MockMemberResult(4, "ping", nil, MemberStateExcluded),
				MockMemberResult(4, "ping", nil, MemberStateExcluded),
			},
			expErr: &ErrMemberExists{Rank: ranklist.NewRankPtr(4)},
		},
		"successful results": {
			results: MemberResults{
				MockMemberResult(2, "ping", nil, MemberStateStopped),
				MockMemberResult(3, "ping", nil, MemberStateExcluded),
				MockMemberResult(4, "ping", nil, MemberStateExcluded),
				MockMemberResult(1, "ping", nil, MemberStateJoined),
			},
			expGroups: []string{"ping\tOK"},
			expOut:    "1-4: ping\tOK\n",
		},
		"mixed results": {
			results: MemberResults{
				MockMemberResult(2, "ping", nil, MemberStateStopped),
				MockMemberResult(3, "ping", nil, MemberStateExcluded),
				MockMemberResult(4, "ping", errors.New("failure\t2"), MemberStateExcluded),
				MockMemberResult(5, "ping", errors.New("failure 2"), MemberStateExcluded),
				MockMemberResult(7, "ping", errors.New("failure 1"), MemberStateExcluded),
				MockMemberResult(6, "ping", errors.New("failure 1"), MemberStateExcluded),
				MockMemberResult(1, "ping", nil, MemberStateJoined),
			},
			expGroups: []string{"ping\tOK", "ping\tfailure 1", "ping\tfailure 2"},
			expOut:    "1-3: ping\tOK\n6-7: ping\tfailure 1\n4-5: ping\tfailure 2\n",
		},
		"separator in error message": {
			results: MemberResults{
				MockMemberResult(2, "ping", nil, MemberStateStopped),
				MockMemberResult(3, "ping", nil, MemberStateExcluded),
				MockMemberResult(4, "ping", errors.New("failure 2"),
					MemberStateExcluded),
				MockMemberResult(5, "ping", errors.New("failure 2"),
					MemberStateExcluded),
			},
			matchingFieldSep: true,
			expErr:           errors.New("illegal field separator"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.rankGroups == nil && !tc.nilRankGroups {
				tc.rankGroups = make(RankGroups)
			}
			sep := "\t"
			if tc.matchingFieldSep {
				sep = " "
			}

			gotErr := tc.rankGroups.FromMemberResults(tc.results, sep)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expGroups, tc.rankGroups.Keys()); diff != "" {
				t.Fatalf("unexpected group states (-want, +got):\n%s\n", diff)
			}

			if diff := cmp.Diff(tc.expOut, tc.rankGroups.String()); diff != "" {
				t.Fatalf("unexpected repr (-want, +got):\n%s\n", diff)
			}
		})
	}
}
