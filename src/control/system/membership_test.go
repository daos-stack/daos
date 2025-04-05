//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system_test

import (
	"context"
	"fmt"
	"net"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	. "github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/fault"
	. "github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

var (
	memberCmpOpts = []cmp.Option{
		cmpopts.IgnoreUnexported(Member{}),
		cmpopts.EquateApproxTime(time.Second),
	}
)

func mockEvtEngineDied(t *testing.T, r uint32) *events.RASEvent {
	t.Helper()
	return events.NewEngineDiedEvent("foo", 0, r, common.NormalExit, 1234)
}

func populateMembership(t *testing.T, log logging.Logger, members ...*Member) *Membership {
	t.Helper()

	db := raft.MockDatabase(t, log)
	ms := MockMembership(t, log, db, MockResolveFn)
	for _, m := range members {
		if _, err := ms.Add(m); err != nil {
			t.Fatal(err)
		}
	}

	return ms
}

func mockStoppedRankOnHost1(t *testing.T, rID int32) *Member {
	addr1, err := net.ResolveTCPAddr("tcp", "127.0.0.1:10001")
	if err != nil {
		t.Fatal(err)
	}
	return MockMemberFullSpec(t, Rank(rID), MockUUID(rID), "", addr1, MemberStateStopped)
}

func TestSystem_Membership_Get(t *testing.T) {
	for name, tc := range map[string]struct {
		memberToAdd *Member
		rankToGet   Rank
		expMember   *Member
		expErr      error
	}{
		"exists": {
			MockMember(t, 1, MemberStateUnknown),
			Rank(1),
			MockMember(t, 1, MemberStateUnknown),
			nil,
		},
		"absent": {
			MockMember(t, 1, MemberStateUnknown),
			Rank(2),
			MockMember(t, 1, MemberStateUnknown),
			ErrMemberRankNotFound(2),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := populateMembership(t, log, tc.memberToAdd)

			m, err := ms.Get(tc.rankToGet)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expMember, m, memberCmpOpts...); diff != "" {
				t.Fatalf("unexpected member (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Membership_AddRemove(t *testing.T) {
	dupeRankMember := MockMember(t, 1, MemberStateUnknown)
	dupeRankMember.UUID = uuid.MustParse(MockUUID(2))
	dupeUUIDMember := MockMember(t, 1, MemberStateUnknown)
	dupeUUIDMember.Rank = 2

	for name, tc := range map[string]struct {
		membersToAdd  Members
		ranksToRemove []Rank
		expMembers    Members
		expAddErrs    []error
	}{
		"add remove success": {
			Members{
				MockMember(t, 1, MemberStateUnknown),
				MockMember(t, 2, MemberStateUnknown),
			},
			[]Rank{1, 2},
			Members{},
			[]error{nil, nil},
		},
		"add failure duplicate rank": {
			Members{
				MockMember(t, 1, MemberStateUnknown),
				dupeRankMember,
			},
			nil,
			nil,
			[]error{nil, ErrRankExists(1)},
		},
		"add failure duplicate UUID": {
			Members{
				MockMember(t, 1, MemberStateUnknown),
				dupeUUIDMember,
			},
			nil,
			nil,
			[]error{nil, ErrUuidExists(dupeUUIDMember.UUID)},
		},
		"remove non-existent": {
			Members{
				MockMember(t, 1, MemberStateUnknown),
				MockMember(t, 2, MemberStateUnknown),
			},
			[]Rank{3},
			Members{
				MockMember(t, 1, MemberStateUnknown),
				MockMember(t, 2, MemberStateUnknown),
			},
			[]error{nil, nil},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			var count int
			var err error
			db := raft.MockDatabase(t, log)
			ms := MockMembership(t, log, db, MockResolveFn)
			for i, m := range tc.membersToAdd {
				count, err = ms.Add(m)
				CmpErr(t, tc.expAddErrs[i], err)
				if tc.expAddErrs[i] != nil {
					return
				}
			}

			AssertEqual(t, len(tc.membersToAdd), count, name)

			for _, r := range tc.ranksToRemove {
				ms.Remove(r)
			}

			count, err = ms.Count()
			if err != nil {
				t.Fatal(err)
			}
			AssertEqual(t, len(tc.expMembers), count, name)
		})
	}
}

func TestSystem_Membership_Add(t *testing.T) {
	m0a := *MockMember(t, 0, MemberStateStopped)
	m1a := *MockMember(t, 1, MemberStateStopped)
	m2a := *MockMember(t, 2, MemberStateStopped)
	m0b := m0a
	m0b.UUID = uuid.MustParse(MockUUID(4)) // uuid changes after reformat
	m0b.State = MemberStateJoined
	m1b := m1a
	m1b.Addr = m0a.Addr // rank allocated differently between hosts after reformat
	m1b.UUID = uuid.MustParse(MockUUID(5))
	m1b.State = MemberStateJoined
	m2b := m2a
	m2a.Addr = m0a.Addr // ranks 0,2 on same host before reformat
	m2b.Addr = m1a.Addr // ranks 0,1 on same host after reformat
	m2b.UUID = uuid.MustParse(MockUUID(6))
	m2b.State = MemberStateJoined

	for name, tc := range map[string]struct {
		membersToAddOrReplace Members
		expMembers            Members
	}{
		"add then update": {
			Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 1, MemberStateStopped),
			},
			Members{MockMember(t, 1, MemberStateStopped)},
		},
		"add multiple": {
			Members{
				MockMember(t, 1, MemberStateUnknown),
				MockMember(t, 2, MemberStateUnknown),
			},
			Members{
				MockMember(t, 1, MemberStateUnknown),
				MockMember(t, 2, MemberStateUnknown),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			db := raft.MockDatabase(t, log)
			ms := MockMembership(t, log, db, MockResolveFn)
			for _, m := range tc.membersToAddOrReplace {
				if err := ms.AddOrReplace(m); err != nil {
					t.Fatal(err)
				}
			}

			count, err := ms.Count()
			if err != nil {
				t.Fatal(err)
			}
			AssertEqual(t, len(tc.expMembers), count, name)

			for _, em := range tc.expMembers {
				m, err := ms.Get(em.Rank)
				if err != nil {
					t.Fatal(err)
				}
				if diff := cmp.Diff(em, m, memberCmpOpts...); diff != "" {
					t.Fatalf("unexpected member (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestSystem_Membership_RankList_Members(t *testing.T) {
	members := Members{
		MockMember(t, 1, MemberStateJoined),
		MockMember(t, 2, MemberStateStopped),
		MockMember(t, 3, MemberStateExcluded),
		mockStoppedRankOnHost1(t, 4),
	}

	for name, tc := range map[string]struct {
		members       Members
		ranks         string
		desiredStates []MemberState
		expRanks      []Rank
		expHostRanks  map[string][]Rank
		expHosts      []string
		expMembers    Members
	}{
		"no rank list": {
			members:  members,
			expRanks: []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{
				"127.0.0.1:10001": {Rank(1), Rank(4)},
				"127.0.0.2:10001": {Rank(2)},
				"127.0.0.3:10001": {Rank(3)},
			},
			expHosts:   []string{"127.0.0.1:10001", "127.0.0.2:10001", "127.0.0.3:10001"},
			expMembers: members,
		},
		"subset rank list": {
			members:  members,
			ranks:    "1-2",
			expRanks: []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{
				"127.0.0.1:10001": {Rank(1)},
				"127.0.0.2:10001": {Rank(2)},
			},
			expHosts: []string{"127.0.0.1:10001", "127.0.0.2:10001"},
			expMembers: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 2, MemberStateStopped),
			},
		},
		"distinct rank list": {
			members:      members,
			ranks:        "0,5",
			expRanks:     []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{},
			expHosts:     []string{},
		},
		"superset rank list": {
			members:  members,
			ranks:    "0-5",
			expRanks: []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{
				"127.0.0.1:10001": {Rank(1), Rank(4)},
				"127.0.0.2:10001": {Rank(2)},
				"127.0.0.3:10001": {Rank(3)},
			},
			expHosts:   []string{"127.0.0.1:10001", "127.0.0.2:10001", "127.0.0.3:10001"},
			expMembers: members,
		},
		"distinct desired states": {
			members:       members,
			desiredStates: []MemberState{MemberStateJoined, MemberStateExcluded},
			expRanks:      []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{
				"127.0.0.1:10001": {Rank(1), Rank(4)},
				"127.0.0.2:10001": {Rank(2)},
				"127.0.0.3:10001": {Rank(3)},
			},
			expHosts: []string{"127.0.0.1:10001", "127.0.0.2:10001", "127.0.0.3:10001"},
			expMembers: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 3, MemberStateExcluded),
			},
		},
		"combined desired states": {
			members:       members,
			desiredStates: []MemberState{MemberStateJoined | MemberStateExcluded},
			expRanks:      []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{
				"127.0.0.1:10001": {Rank(1), Rank(4)},
				"127.0.0.2:10001": {Rank(2)},
				"127.0.0.3:10001": {Rank(3)},
			},
			expHosts: []string{"127.0.0.1:10001", "127.0.0.2:10001", "127.0.0.3:10001"},
			expMembers: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 3, MemberStateExcluded),
			},
		},
		"desired states; not joined": {
			members:       members,
			desiredStates: []MemberState{AllMemberFilter &^ MemberStateJoined},
			expRanks:      []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{
				"127.0.0.1:10001": {Rank(1), Rank(4)},
				"127.0.0.2:10001": {Rank(2)},
				"127.0.0.3:10001": {Rank(3)},
			},
			expHosts: []string{"127.0.0.1:10001", "127.0.0.2:10001", "127.0.0.3:10001"},
			expMembers: Members{
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateExcluded),
				mockStoppedRankOnHost1(t, 4),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := populateMembership(t, log, tc.members...)

			rankSet, err := CreateRankSet(tc.ranks)
			if err != nil {
				t.Fatal(err)
			}

			rankList, err := ms.RankList()
			if err != nil {
				t.Fatal(err)
			}

			AssertEqual(t, tc.expRanks, rankList, "ranks")
			AssertEqual(t, tc.expHostRanks, ms.HostRanks(rankSet), "host ranks")
			AssertEqual(t, tc.expHosts, ms.HostList(rankSet), "hosts")

			members, err := ms.Members(rankSet, tc.desiredStates...)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expMembers, members, memberCmpOpts...); diff != "" {
				t.Fatalf("unexpected members (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Membership_CheckRanklist(t *testing.T) {
	members := Members{
		MockMember(t, 0, MemberStateJoined),
		MockMember(t, 1, MemberStateJoined),
		MockMember(t, 2, MemberStateStopped),
		MockMember(t, 3, MemberStateExcluded),
		mockStoppedRankOnHost1(t, 4),
	}

	for name, tc := range map[string]struct {
		members    Members
		inRanklist string
		expRanks   string
		expMissing string
		expErr     error
	}{
		"no rank list": {
			members:  members,
			expRanks: "0-4",
		},
		"bad rank list": {
			members:    members,
			inRanklist: "foobar",
			expErr:     errors.New("unexpected alphabetic character(s)"),
		},
		"no members": {
			inRanklist: "0-4",
			expMissing: "0-4",
		},
		"full ranklist": {
			members:    members,
			inRanklist: "0-4",
			expRanks:   "0-4",
		},
		"partial ranklist": {
			members:    members,
			inRanklist: "3-4",
			expRanks:   "3-4",
		},
		"oversubscribed ranklist": {
			members:    members[:3],
			inRanklist: "0-4",
			expMissing: "3-4",
			expRanks:   "0-2",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := populateMembership(t, log, tc.members...)

			hit, miss, err := ms.CheckRanks(tc.inRanklist)
			CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			AssertEqual(t, tc.expRanks, hit.String(), "extant ranks")
			AssertEqual(t, tc.expMissing, miss.String(), "missing ranks")
		})
	}
}

func TestSystem_Membership_CheckHostlist(t *testing.T) {
	members := Members{
		MockMember(t, 1, MemberStateJoined),
		MockMember(t, 2, MemberStateStopped),
		MockMember(t, 3, MemberStateExcluded),
		MockMember(t, 4, MemberStateJoined),
		MockMember(t, 5, MemberStateJoined),
		mockStoppedRankOnHost1(t, 6),
	}

	for name, tc := range map[string]struct {
		members         Members
		inHosts         string
		expRanks        string
		expMissingHosts string
		expErr          error
	}{
		"no host list": {
			members: members,
		},
		"bad host list": {
			members: members,
			inHosts: "123,foobar",
			expErr:  errors.New("invalid hostname"),
		},
		"no members with ip & port": {
			inHosts:         "127.0.0.[1-3]:10001",
			expMissingHosts: "127.0.0.[1-3]:10001",
		},
		"no members with ip": {
			inHosts:         "127.0.0.[1-3]",
			expMissingHosts: "127.0.0.[1-3]",
		},
		"no members with hostname": {
			inHosts:         "foo-[1-3]",
			expMissingHosts: "foo-[1-3]",
		},
		"ips can resolve": {
			members:  members,
			inHosts:  "127.0.0.[1-3]:10001",
			expRanks: "1-3,6",
		},
		"ips can't resolve": {
			members:         members,
			inHosts:         "127.0.0.[1-5]",
			expRanks:        "1-3,6",
			expMissingHosts: "127.0.0.[4-5]",
		},
		"ips can't resolve with port": {
			members:         members,
			inHosts:         "127.0.0.[1-5]:10001",
			expRanks:        "1-3,6",
			expMissingHosts: "127.0.0.[4-5]:10001",
		},
		"ips can't resolve bad port": {
			members:         members,
			inHosts:         "127.0.0.[1-2]:10000,127.0.0.3:10001",
			expRanks:        "3",
			expMissingHosts: "127.0.0.[1-2]:10000",
		},
		"ips partial ranklist": {
			members:  members,
			inHosts:  "127.0.0.[1-2]:10001",
			expRanks: "1-2,6",
		},
		"ips oversubscribed ranklist": {
			members:         members,
			inHosts:         "127.0.0.[0-9]:10001",
			expRanks:        "1-3,6",
			expMissingHosts: "127.0.0.[0,4-9]:10001",
		},
		"hostnames can resolve": {
			members:  members,
			inHosts:  "foo-[1-5]",
			expRanks: "1-6",
		},
		"hostnames can't resolve with port": {
			members:  members,
			inHosts:  "foo-[1-5]:10001",
			expRanks: "1-6",
		},
		"hostnames can't resolve bad port": {
			members:         members,
			inHosts:         "foo-[1-2]:10000,foo-3:10001",
			expRanks:        "3",
			expMissingHosts: "foo-[1-2]:10000",
		},
		"hostnames partial ranklist": {
			members:  members,
			inHosts:  "foo-[1-3]:10001",
			expRanks: "1-3,6",
		},
		"hostnames oversubscribed ranklist": {
			members:         members,
			inHosts:         "foo-[0-9]:10001",
			expRanks:        "1-6",
			expMissingHosts: "foo-[0,6-9]:10001",
		},
		"hostnames oversubscribed without port": {
			members:         members,
			inHosts:         "foo-[5-7]",
			expRanks:        "5",
			expMissingHosts: "foo-[6-7]",
		},
		"different local addresses": {
			members:         members,
			inHosts:         "10.0.0.[1-3]",
			expMissingHosts: "10.0.0.[1-3]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := populateMembership(t, log, tc.members...)

			rankSet, missingHostSet, err := ms.CheckHosts(tc.inHosts, 10001)
			CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			AssertEqual(t, tc.expRanks, rankSet.String(), "extant ranks")
			AssertEqual(t, tc.expMissingHosts, missingHostSet.String(), "missing hosts")
		})
	}
}

func TestSystem_Membership_UpdateMemberStates(t *testing.T) {
	// blank host address should get updated to that of member
	mrDiffAddr1 := NewMemberResult(1, nil, MemberStateReady)
	mrDiffAddr1.Addr = ""
	// existing host address should not get updated to that of member
	mrDiffAddr2 := NewMemberResult(2, errors.New("can't stop"), MemberStateErrored)
	mrDiffAddr2.Addr = "10.0.0.1"
	expMrDiffAddr1 := mrDiffAddr1
	expMrDiffAddr1.Addr = "192.168.1.1:10001"
	expMrDiffAddr2 := mrDiffAddr2

	for name, tc := range map[string]struct {
		ignoreErrs bool
		members    Members
		results    MemberResults
		expMembers Members
		expResults MemberResults
		expErr     error
	}{
		"update result address from member": {
			members: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 2, MemberStateStopped),
			},
			results: MemberResults{
				mrDiffAddr1,
				mrDiffAddr2,
			},
			expMembers: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 2, MemberStateErrored, "can't stop"),
			},
			expResults: MemberResults{
				expMrDiffAddr1,
				expMrDiffAddr2,
			},
		},
		"ignore errored results": {
			ignoreErrs: true,
			members: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateExcluded),
				MockMember(t, 4, MemberStateStopped),
				MockMember(t, 5, MemberStateJoined),
				MockMember(t, 6, MemberStateJoined),
			},
			results: MemberResults{
				NewMemberResult(1, nil, MemberStateStopped),
				NewMemberResult(2, errors.New("can't stop"), MemberStateErrored),
				NewMemberResult(4, nil, MemberStateReady),
				NewMemberResult(5, nil, MemberStateReady),
				&MemberResult{Rank: 6, Msg: "exit 1", State: MemberStateStopped},
			},
			expMembers: Members{
				MockMember(t, 1, MemberStateStopped),
				MockMember(t, 2, MemberStateStopped), // errored results don't change member state
				MockMember(t, 3, MemberStateExcluded),
				MockMember(t, 4, MemberStateReady),
				MockMember(t, 5, MemberStateJoined), // "Joined" will not be updated to "Ready"
				MockMember(t, 6, MemberStateStopped, "exit 1"),
			},
		},
		"don't ignore errored results": {
			members: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateExcluded),
				MockMember(t, 4, MemberStateStopped),
				MockMember(t, 5, MemberStateJoined),
				MockMember(t, 6, MemberStateStopped),
			},
			results: MemberResults{
				NewMemberResult(1, nil, MemberStateStopped),
				NewMemberResult(2, errors.New("can't stop"), MemberStateErrored),
				NewMemberResult(4, nil, MemberStateReady),
				NewMemberResult(5, nil, MemberStateReady),
				&MemberResult{Rank: 6, Msg: "exit 1", State: MemberStateStopped},
			},
			expMembers: Members{
				MockMember(t, 1, MemberStateStopped),
				MockMember(t, 2, MemberStateErrored, "can't stop"),
				MockMember(t, 3, MemberStateExcluded),
				MockMember(t, 4, MemberStateReady),
				MockMember(t, 5, MemberStateJoined),
				MockMember(t, 6, MemberStateStopped),
			},
		},
		"errored result with nonerrored state": {
			members: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateExcluded),
			},
			results: MemberResults{
				NewMemberResult(1, nil, MemberStateStopped),
				NewMemberResult(2, errors.New("can't stop"), MemberStateErrored),
				func() *MemberResult {
					mr := NewMemberResult(3, errors.New("can't stop"),
						MemberStateJoined)
					mr.State = MemberStateJoined
					return mr
				}(),
			},
			expErr: errors.New("rank 3 has conflicting state 'Joined'"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := populateMembership(t, log, tc.members...)

			// members should be updated with result state
			CmpErr(t, tc.expErr, ms.UpdateMemberStates(tc.results, !tc.ignoreErrs))
			if tc.expErr != nil {
				return
			}

			cmpOpts := append(memberCmpOpts,
				cmpopts.IgnoreUnexported(MemberResult{}),
			)

			members, err := ms.Members(nil)
			if err != nil {
				t.Fatal(err)
			}

			for i, m := range members {
				if diff := cmp.Diff(tc.expMembers[i], m, cmpOpts...); diff != "" {
					t.Fatalf("unexpected member (-want, +got)\n%s\n", diff)
				}
			}

			// verify result host address is updated to that of member if empty
			for i, r := range tc.expResults {
				AssertEqual(t, tc.expResults[i].Addr, tc.results[i].Addr, r.Rank.String())
			}
		})
	}
}

func TestSystem_Membership_FindRankFromJoinRequest(t *testing.T) {
	fd1 := MustCreateFaultDomainFromString("/dc1/rack8/pdu5/host1")
	fd2 := MustCreateFaultDomainFromString("/dc1/rack9/pdu0/host2")

	defaultCurMembers := make([]*Member, 2)
	for i := range defaultCurMembers {
		defaultCurMembers[i] = MockMember(t, uint32(i), MemberStateJoined).WithFaultDomain(fd1)
	}
	curMember := defaultCurMembers[1]
	newUUID := uuid.New()
	newMember := MockMember(t, 2, MemberStateJoined).WithFaultDomain(fd2)

	for name, tc := range map[string]struct {
		curMembers []*Member
		req        *JoinRequest
		expRank    Rank
		expErr     error
	}{
		"non-nil rank in request": {
			req: &JoinRequest{
				Rank:             curMember.Rank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
			},
			expErr: errors.New("unexpected rank"),
		},
		"empty membership": {
			curMembers: []*Member{},
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
				FaultDomain:      curMember.FaultDomain,
			},
			expErr: errors.New("empty system membership"),
		},
		"no matching member": {
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             newMember.UUID,
				ControlAddr:      newMember.Addr,
				PrimaryFabricURI: newMember.Addr.String(),
				FabricContexts:   newMember.PrimaryFabricContexts,
				FaultDomain:      newMember.FaultDomain,
			},
			expErr: FaultJoinReplaceRankNotFound(4), // Takes nr not matching fields
		},
		"partially matching member": {
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             newMember.UUID,
				ControlAddr:      newMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
				FaultDomain:      curMember.FaultDomain,
			},
			expErr: FaultJoinReplaceRankNotFound(1), // Diff resolution when nr == 1
		},
		"matching member; identical UUID": {
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
				FaultDomain:      curMember.FaultDomain,
			},
			expErr: ErrUuidExists(curMember.UUID),
		},
		"different fault domain": {
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
				FaultDomain:      fd2,
			},
			expErr: FaultJoinReplaceRankNotFound(1),
		},
		"success; matching member": {
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             newUUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
				FaultDomain:      curMember.FaultDomain,
			},
			expRank: curMember.Rank,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			db := raft.MockDatabase(t, log)
			ms := MockMembership(t, log, db, MockResolveFn)

			if tc.curMembers == nil {
				tc.curMembers = defaultCurMembers
			}
			for _, curM := range tc.curMembers {
				if err := db.AddMember(curM); err != nil {
					t.Fatal(err)
				}
			}

			gotRank, gotErr := ms.FindRankFromJoinRequest(tc.req)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				if !fault.IsFault(tc.expErr) {
					if fault.IsFault(gotErr) {
						t.Fatal("error comparison, one is not a fault")
					}
					return
				}
				if !fault.IsFault(gotErr) {
					t.Fatal("error comparison, one is not a fault")
				}
				// Compare Fault resolution messages.
				er := fault.ShowResolutionFor(tc.expErr)
				gr := fault.ShowResolutionFor(gotErr)
				if diff := cmp.Diff(er, gr); diff != "" {
					t.Fatalf("unexpected fault resolution (-want, +got):\n%s\n", diff)
				}
				return
			}

			test.AssertEqual(t, tc.expRank, gotRank, "unexpected found rank value")
		})
	}
}

func TestSystem_Membership_Join(t *testing.T) {
	fd1 := MustCreateFaultDomainFromString("/dc1/rack8/pdu5/host1")
	fd2 := MustCreateFaultDomainFromString("/dc1/rack9/pdu0/host2")
	shallowFD := MustCreateFaultDomainFromString("/host3")

	defaultCurMembers := make([]*Member, 2)
	for i := range defaultCurMembers {
		defaultCurMembers[i] = MockMember(t, uint32(i), MemberStateJoined).WithFaultDomain(fd1)
	}
	curMember := defaultCurMembers[0]
	newUUID := uuid.New()
	newMember := MockMember(t, 2, MemberStateJoined).WithFaultDomain(fd2)
	newMemberShallowFD := MockMember(t, 3, MemberStateJoined).WithFaultDomain(shallowFD)
	adminExcludedMember := MockMember(t, 3, MemberStateAdminExcluded)

	expMapVer := uint32(len(defaultCurMembers) + 1)

	for name, tc := range map[string]struct {
		notLeader  bool
		curMembers []*Member
		req        *JoinRequest
		expResp    *JoinResponse
		expErr     error
	}{
		"not leader": {
			notLeader: true,
			req: &JoinRequest{
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      fd1,
			},
			expErr: errors.New("leader"),
		},
		"successful rejoin": {
			req: &JoinRequest{
				Rank:             curMember.Rank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      curMember.FaultDomain,
			},
			expResp: &JoinResponse{
				Member:     curMember,
				PrevState:  curMember.State,
				MapVersion: expMapVer,
			},
		},
		"successful rejoin with different fault domain": {
			req: &JoinRequest{
				Rank:             curMember.Rank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      fd2,
			},
			expResp: &JoinResponse{
				Member:     MockMember(t, 0, MemberStateJoined).WithFaultDomain(fd2),
				PrevState:  curMember.State,
				MapVersion: expMapVer,
			},
		},
		"rejoin with existing UUID and unknown rank": {
			req: &JoinRequest{
				Rank:             Rank(42),
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      curMember.FaultDomain,
			},
			expErr: ErrUuidExists(curMember.UUID),
		},
		"rejoin with different UUID and dupe rank": {
			req: &JoinRequest{
				Rank:             curMember.Rank,
				UUID:             newUUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      curMember.FaultDomain,
			},
			expErr: ErrUuidChanged(newUUID, curMember.UUID, curMember.Rank),
		},
		"rejoin with different address": {
			req: &JoinRequest{
				Rank:             curMember.Rank,
				UUID:             curMember.UUID,
				ControlAddr:      newMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      curMember.FaultDomain,
			},
			expErr: ErrControlAddrChanged(newMember.Addr, curMember.Addr, curMember.UUID, curMember.Rank),
		},
		"successful join": {
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             newMember.UUID,
				ControlAddr:      newMember.Addr,
				PrimaryFabricURI: newMember.Addr.String(),
				FabricContexts:   newMember.PrimaryFabricContexts,
				FaultDomain:      newMember.FaultDomain,
			},
			expResp: &JoinResponse{
				Created:    true,
				Member:     newMember,
				PrevState:  MemberStateUnknown,
				MapVersion: expMapVer,
			},
		},
		"replace; nil rank in request": {
			req: &JoinRequest{
				Replace:          true,
				Rank:             NilRank,
				UUID:             newMember.UUID,
				ControlAddr:      newMember.Addr,
				PrimaryFabricURI: newMember.Addr.String(),
				FabricContexts:   newMember.PrimaryFabricContexts,
				FaultDomain:      newMember.FaultDomain,
			},
			expErr: errors.New("unexpected nil rank"),
		},
		"replace; unknown rank in request": {
			req: &JoinRequest{
				Replace:          true,
				Rank:             newMember.Rank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
				FaultDomain:      curMember.FaultDomain,
			},
			expErr: ErrMemberRankNotFound(2),
		},
		"replace; administrative excluded rank in request": {
			curMembers: []*Member{
				adminExcludedMember,
			},
			req: &JoinRequest{
				Replace:          true,
				Rank:             0, // Rank is index of manually added tc.curMembers.
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
				FaultDomain:      curMember.FaultDomain,
			},
			expErr: ErrAdminExcluded(adminExcludedMember.UUID, 0),
		},
		"successful replace; different UUID but otherwise identical member": {
			req: &JoinRequest{
				Replace:          true,
				Rank:             curMember.Rank,
				UUID:             newUUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      curMember.FaultDomain,
			},
			expResp: &JoinResponse{
				Created: false,
				Member: func() *Member {
					cm := *defaultCurMembers[0]
					cm.UUID = newUUID
					return &cm
				}(),
				PrevState: curMember.State,
				// Extra map increment because of remove and add operations.
				MapVersion: expMapVer + 1,
			},
		},
		// DAOS-15947 TODO: This should probably be refused as duplicate addresses/URIs
		//                  rather than joining a new rank.
		"rejoin identical member with new UUID and nil rank; replace not set": {
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             newUUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      curMember.FaultDomain,
			},
			expResp: &JoinResponse{
				Created: true,
				Member: func() *Member {
					cm := *defaultCurMembers[0]
					cm.UUID = newUUID
					cm.Rank = 2
					return &cm
				}(),
				PrevState:  MemberStateUnknown,
				MapVersion: expMapVer,
			},
		},
		"new member with bad fault domain depth": {
			req: &JoinRequest{
				Rank:             NilRank,
				UUID:             newMemberShallowFD.UUID,
				ControlAddr:      newMemberShallowFD.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   newMemberShallowFD.PrimaryFabricContexts,
				FaultDomain:      newMemberShallowFD.FaultDomain,
			},
			expErr: FaultBadFaultDomainDepth(newMemberShallowFD.FaultDomain, curMember.FaultDomain.NumLevels()),
		},
		"update existing member with bad fault domain depth": {
			req: &JoinRequest{
				Rank:             curMember.Rank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FabricContexts:   curMember.PrimaryFabricContexts,
				FaultDomain:      shallowFD,
			},
			expErr: FaultBadFaultDomainDepth(newMemberShallowFD.FaultDomain, curMember.FaultDomain.NumLevels()),
		},
		"change fault domain depth for only member": {
			curMembers: []*Member{
				curMember,
			},
			req: &JoinRequest{
				Rank:             curMember.Rank,
				UUID:             curMember.UUID,
				ControlAddr:      curMember.Addr,
				PrimaryFabricURI: curMember.Addr.String(),
				FaultDomain:      shallowFD,
			},
			expResp: &JoinResponse{
				Member:     MockMember(t, 0, MemberStateJoined).WithFaultDomain(shallowFD),
				PrevState:  curMember.State,
				MapVersion: 2,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			db := raft.MockDatabase(t, log)
			ms := MockMembership(t, log, db, MockResolveFn)

			if tc.curMembers == nil {
				tc.curMembers = defaultCurMembers
			}
			for _, curM := range tc.curMembers {
				curM.Rank = NilRank // Assign ranks based on order they are added.
				if err := db.AddMember(curM); err != nil {
					t.Fatal(err)
				}
			}
			if tc.notLeader {
				_ = db.ShutdownRaft()
			}

			gotResp, gotErr := ms.Join(tc.req)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, memberCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Membership_OnEvent(t *testing.T) {
	members := Members{
		MockMember(t, 0, MemberStateJoined),
		MockMember(t, 1, MemberStateJoined),
		MockMember(t, 2, MemberStateStopped),
		MockMember(t, 3, MemberStateExcluded),
	}

	for name, tc := range map[string]struct {
		members    Members
		event      *events.RASEvent
		expMembers Members
	}{
		"nil event": {
			members:    members,
			event:      nil,
			expMembers: members,
		},
		"event on unrecognized rank": {
			members:    members,
			event:      mockEvtEngineDied(t, 4),
			expMembers: members,
		},
		"state updated on unscheduled exit": {
			members: members,
			event:   mockEvtEngineDied(t, 1),
			expMembers: Members{
				MockMember(t, 0, MemberStateJoined),
				MockMember(t, 1, MemberStateErrored).WithInfo(
					errors.Wrap(common.NormalExit,
						"DAOS engine 0 exited unexpectedly").Error()),
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateExcluded),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := populateMembership(t, log, tc.members...)

			ctx, cancel := context.WithTimeout(test.Context(t), 50*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			defer ps.Close()

			ps.Subscribe(events.RASTypeStateChange, ms)

			ps.Publish(tc.event)

			<-ctx.Done()

			members, err := ms.Members(nil)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expMembers, members, memberCmpOpts...); diff != "" {
				t.Errorf("unexpected membership (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Membership_MarkDead(t *testing.T) {
	for name, tc := range map[string]struct {
		rank        Rank
		incarnation uint64
		expErr      error
	}{
		"unknown member": {
			rank:   42,
			expErr: ErrMemberRankNotFound(42),
		},
		"invalid transition ignored": {
			rank:   2,
			expErr: errors.New("illegal member state update"),
		},
		"stale event for joined member": {
			rank:        0,
			incarnation: 1,
			expErr:      errors.New("incarnation"),
		},
		"new event for joined member": {
			rank:        0,
			incarnation: 2,
		},
		"event for stopped member": {
			rank:        1,
			incarnation: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			mock := func(rank uint32, inc uint64, state MemberState) *Member {
				m := MockMember(t, rank, state)
				m.Incarnation = inc
				return m
			}

			ms := populateMembership(t, log,
				mock(0, 2, MemberStateJoined),
				mock(1, 2, MemberStateStopped),
				mock(2, 2, MemberStateExcluded),
			)

			gotErr := ms.MarkRankDead(tc.rank, tc.incarnation)
			CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSystem_Membership_CompressedFaultDomainTree(t *testing.T) {
	testMemberWithFaultDomain := func(rank Rank, faultDomain *FaultDomain) *Member {
		return &Member{
			Rank:        rank,
			FaultDomain: faultDomain,
		}
	}

	rankDomain := func(parent string, rank uint32) *FaultDomain {
		parentFd := MustCreateFaultDomainFromString(parent)
		member := testMemberWithFaultDomain(Rank(rank), parentFd)
		return MemberFaultDomain(member)
	}

	for name, tc := range map[string]struct {
		tree       *FaultDomainTree
		inputRanks []uint32
		expResult  []uint32
		expErr     error
	}{
		"nil tree": {
			expErr: errors.New("uninitialized fault domain tree"),
		},
		"root only": {
			tree: NewFaultDomainTree(),
			expResult: []uint32{
				0, // metadata
				0,
				ExpFaultDomainID(0),
				0,
			},
		},
		"single branch, no rank leaves": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomain("one", "two", "three"),
			),
			expResult: []uint32{
				DomTreeMetadataHasPerfDom, // metadata
				3,
				ExpFaultDomainID(0),
				1,
				2,
				ExpFaultDomainID(1),
				1,
				1,
				ExpFaultDomainID(2),
				1,
				0,
				ExpFaultDomainID(3),
				0,
			},
		},
		"multi branch, no rank leaves": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/rack0/pdu0"),
				MustCreateFaultDomainFromString("/rack0/pdu1"),
				MustCreateFaultDomainFromString("/rack1/pdu2"),
				MustCreateFaultDomainFromString("/rack1/pdu3"),
				MustCreateFaultDomainFromString("/rack2/pdu4"),
			),
			expResult: []uint32{
				0, // metadata
				2, // root
				ExpFaultDomainID(0),
				3,
				1, // rack0
				ExpFaultDomainID(1),
				2,
				1, // rack1
				ExpFaultDomainID(4),
				2,
				1, // rack2
				ExpFaultDomainID(7),
				1,
				0, // pdu0
				ExpFaultDomainID(2),
				0,
				0, // pdu1
				ExpFaultDomainID(3),
				0,
				0, // pdu2
				ExpFaultDomainID(5),
				0,
				0, // pdu3
				ExpFaultDomainID(6),
				0,
				0, // pdu4
				ExpFaultDomainID(8),
				0,
			},
		},
		"single branch with rank leaves": {
			tree: NewFaultDomainTree(
				rankDomain("/one/two/three", 5),
			),
			expResult: []uint32{
				DomTreeMetadataHasPerfDom, // metadata
				4,
				ExpFaultDomainID(0),
				1,
				3,
				ExpFaultDomainID(1),
				1,
				2,
				ExpFaultDomainID(2),
				1,
				1,
				ExpFaultDomainID(3),
				1,
				5,
			},
		},
		"multi branch with rank leaves": {
			tree: NewFaultDomainTree(
				rankDomain("/rack0/pdu0", 0),
				rankDomain("/rack0/pdu1", 1),
				rankDomain("/rack1/pdu2", 2),
				rankDomain("/rack1/pdu3", 3),
				rankDomain("/rack1/pdu3", 4),
				rankDomain("/rack2/pdu4", 5),
			),
			expResult: []uint32{
				DomTreeMetadataHasPerfDom, // metadata
				3,
				ExpFaultDomainID(0), // root
				3,
				2,
				ExpFaultDomainID(1), // rack0
				2,
				2,
				ExpFaultDomainID(6), // rack1
				2,
				2,
				ExpFaultDomainID(12), // rack2
				1,
				1,
				ExpFaultDomainID(2), // pdu0
				1,
				1,
				ExpFaultDomainID(4), // pdu1
				1,
				1,
				ExpFaultDomainID(7), // pdu2
				1,
				1,
				ExpFaultDomainID(9), // pdu3
				2,
				1,
				ExpFaultDomainID(13), // pdu4
				1,
				// ranks
				0,
				1,
				2,
				3,
				4,
				5,
			},
		},
		"intermediate domain has name like rank": {
			tree: NewFaultDomainTree(
				rankDomain(fmt.Sprintf("/top/%s2/bottom", RankFaultDomainPrefix), 1),
			),
			expResult: []uint32{
				DomTreeMetadataHasPerfDom, // metadata
				4,
				ExpFaultDomainID(0), // root
				1,
				3,
				ExpFaultDomainID(1), // top
				1,
				2,
				ExpFaultDomainID(2), // rank2
				1,
				1,
				ExpFaultDomainID(3), // bottom
				1,
				1, // rank
			},
		},
		"request one rank with node only": {
			tree: NewFaultDomainTree(
				rankDomain("/node0", 0),
				rankDomain("/node1", 1),
				rankDomain("/node2", 2),
				rankDomain("/node3", 3),
				rankDomain("/node3", 4),
				rankDomain("/node4", 5),
			),
			inputRanks: []uint32{4},
			expResult: []uint32{
				0, // metadata
				2,
				ExpFaultDomainID(0), // root
				1,
				1,
				ExpFaultDomainID(7), // node3
				1,
				// ranks
				4,
			},
		},
		"request one rank": {
			tree: NewFaultDomainTree(
				rankDomain("/rack0/pdu0", 0),
				rankDomain("/rack0/pdu1", 1),
				rankDomain("/rack1/pdu2", 2),
				rankDomain("/rack1/pdu3", 3),
				rankDomain("/rack1/pdu3", 4),
				rankDomain("/rack2/pdu4", 5),
			),
			inputRanks: []uint32{4},
			expResult: []uint32{
				DomTreeMetadataHasPerfDom, // metadata
				3,
				ExpFaultDomainID(0), // root
				1,
				2,
				ExpFaultDomainID(6), // rack1
				1,
				1,
				ExpFaultDomainID(9), // pdu3
				1,
				// ranks
				4,
			},
		},
		"request multiple ranks": {
			tree: NewFaultDomainTree(
				rankDomain("/rack0/pdu0", 0),
				rankDomain("/rack0/pdu1", 1),
				rankDomain("/rack1/pdu2", 2),
				rankDomain("/rack1/pdu3", 3),
				rankDomain("/rack1/pdu3", 4),
				rankDomain("/rack2/pdu4", 5),
			),
			inputRanks: []uint32{4, 0, 5, 3},
			expResult: []uint32{
				DomTreeMetadataHasPerfDom, // metadata
				3,
				ExpFaultDomainID(0), // root
				3,
				2,
				ExpFaultDomainID(1), // rack0
				1,
				2,
				ExpFaultDomainID(6), // rack1
				1,
				2,
				ExpFaultDomainID(12), // rack2
				1,
				1,
				ExpFaultDomainID(2), // pdu0
				1,
				1,
				ExpFaultDomainID(9), // pdu3
				2,
				1,
				ExpFaultDomainID(13), // pdu4
				1,
				// ranks
				0,
				3,
				4,
				5,
			},
		},
		"request nonexistent rank": {
			tree: NewFaultDomainTree(
				rankDomain("/rack0/pdu0", 0),
				rankDomain("/rack0/pdu1", 1),
				rankDomain("/rack1/pdu2", 2),
				rankDomain("/rack1/pdu3", 3),
				rankDomain("/rack1/pdu3", 4),
				rankDomain("/rack2/pdu4", 5),
			),
			inputRanks: []uint32{4, 0, 5, 3, 100},
			expErr:     errors.New("rank 100 not found"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			db := raft.MockDatabaseWithFaultDomainTree(t, log, tc.tree)
			membership := NewMembership(log, db)

			result, err := membership.CompressedFaultDomainTree(tc.inputRanks...)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}
