//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

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
	. "github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
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

	ms, _ := MockMembership(t, log, mockResolveFn)
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
			&ErrMemberNotFound{byRank: NewRankPtr(2)},
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
			[]error{nil, errRankExists(1)},
		},
		"add failure duplicate UUID": {
			Members{
				MockMember(t, 1, MemberStateUnknown),
				dupeUUIDMember,
			},
			nil,
			nil,
			[]error{nil, errUuidExists(dupeUUIDMember.UUID)},
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
			ms, _ := MockMembership(t, log, mockResolveFn)
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
	m0b.state = MemberStateJoined
	m1b := m1a
	m1b.Addr = m0a.Addr // rank allocated differently between hosts after reformat
	m1b.UUID = uuid.MustParse(MockUUID(5))
	m1b.state = MemberStateJoined
	m2b := m2a
	m2a.Addr = m0a.Addr // ranks 0,2 on same host before reformat
	m2b.Addr = m1a.Addr // ranks 0,1 on same host after reformat
	m2b.UUID = uuid.MustParse(MockUUID(6))
	m2b.state = MemberStateJoined

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

			ms, _ := MockMembership(t, log, mockResolveFn)
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

func TestSystem_Membership_HostRanks(t *testing.T) {
	members := Members{
		MockMember(t, 1, MemberStateJoined),
		MockMember(t, 2, MemberStateStopped),
		MockMember(t, 3, MemberStateExcluded),
		mockStoppedRankOnHost1(t, 4),
	}

	for name, tc := range map[string]struct {
		members      Members
		ranks        string
		expRanks     []Rank
		expHostRanks map[string][]Rank
		expHosts     []string
		expMembers   Members
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
			if diff := cmp.Diff(tc.expMembers, ms.Members(rankSet), memberCmpOpts...); diff != "" {
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

func mockResolveFn(netString string, address string) (*net.TCPAddr, error) {
	if netString != "tcp" {
		return nil, errors.Errorf("unexpected network type in test: %s, want 'tcp'", netString)
	}

	return map[string]*net.TCPAddr{
			"127.0.0.1:10001": {IP: net.ParseIP("127.0.0.1"), Port: 10001},
			"127.0.0.2:10001": {IP: net.ParseIP("127.0.0.2"), Port: 10001},
			"127.0.0.3:10001": {IP: net.ParseIP("127.0.0.3"), Port: 10001},
			"foo-1:10001":     {IP: net.ParseIP("127.0.0.1"), Port: 10001},
			"foo-2:10001":     {IP: net.ParseIP("127.0.0.2"), Port: 10001},
			"foo-3:10001":     {IP: net.ParseIP("127.0.0.3"), Port: 10001},
			"foo-4:10001":     {IP: net.ParseIP("127.0.0.4"), Port: 10001},
			"foo-5:10001":     {IP: net.ParseIP("127.0.0.5"), Port: 10001},
		}[address], map[string]error{
			"127.0.0.4:10001": errors.New("bad lookup"),
			"127.0.0.5:10001": errors.New("bad lookup"),
			"foo-6:10001":     errors.New("bad lookup"),
		}[address]
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
		expErrMsg  string
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
				NewMemberResult(3, errors.New("can't stop"), MemberStateJoined),
			},
			expErrMsg: "errored result for rank 3 has conflicting state 'Joined'",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := populateMembership(t, log, tc.members...)

			// members should be updated with result state
			err := ms.UpdateMemberStates(tc.results, !tc.ignoreErrs)
			ExpectError(t, err, tc.expErrMsg, name)
			if err != nil {
				return
			}

			cmpOpts := append(memberCmpOpts,
				cmpopts.IgnoreUnexported(MemberResult{}),
			)
			for i, m := range ms.Members(nil) {
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
				FaultDomain: fd1,
			},
			expErr: errors.New("leader"),
		},
		"successful rejoin": {
			req: &JoinRequest{
				Rank:        curMember.Rank,
				UUID:        curMember.UUID,
				ControlAddr: curMember.Addr,
				FabricURI:   curMember.Addr.String(),
				FaultDomain: curMember.FaultDomain,
			},
			expResp: &JoinResponse{
				Member:     curMember,
				PrevState:  curMember.state,
				MapVersion: expMapVer,
			},
		},
		"successful rejoin with different fault domain": {
			req: &JoinRequest{
				Rank:        curMember.Rank,
				UUID:        curMember.UUID,
				ControlAddr: curMember.Addr,
				FabricURI:   curMember.Addr.String(),
				FaultDomain: fd2,
			},
			expResp: &JoinResponse{
				Member:     MockMember(t, 0, MemberStateJoined).WithFaultDomain(fd2),
				PrevState:  curMember.state,
				MapVersion: expMapVer,
			},
		},
		"rejoin with existing UUID and unknown rank": {
			req: &JoinRequest{
				Rank:        Rank(42),
				UUID:        curMember.UUID,
				ControlAddr: curMember.Addr,
				FabricURI:   curMember.Addr.String(),
				FaultDomain: curMember.FaultDomain,
			},
			expErr: errUuidExists(curMember.UUID),
		},
		"rejoin with different UUID and dupe rank": {
			req: &JoinRequest{
				Rank:        curMember.Rank,
				UUID:        newUUID,
				ControlAddr: curMember.Addr,
				FabricURI:   curMember.Addr.String(),
				FaultDomain: curMember.FaultDomain,
			},
			expErr: errUuidChanged(newUUID, curMember.UUID, curMember.Rank),
		},
		"successful join": {
			req: &JoinRequest{
				Rank:           NilRank,
				UUID:           newMember.UUID,
				ControlAddr:    newMember.Addr,
				FabricURI:      newMember.FabricURI,
				FabricContexts: newMember.FabricContexts,
				FaultDomain:    newMember.FaultDomain,
			},
			expResp: &JoinResponse{
				Created:    true,
				Member:     newMember,
				PrevState:  MemberStateUnknown,
				MapVersion: expMapVer,
			},
		},
		"rejoin with existing UUID and nil rank": {
			req: &JoinRequest{
				Rank:        NilRank,
				UUID:        curMember.UUID,
				ControlAddr: curMember.Addr,
				FabricURI:   curMember.Addr.String(),
				FaultDomain: curMember.FaultDomain,
			},
			expResp: &JoinResponse{
				Created:    false,
				Member:     curMember,
				PrevState:  curMember.state,
				MapVersion: expMapVer,
			},
		},
		"new member with bad fault domain depth": {
			req: &JoinRequest{
				Rank:           NilRank,
				UUID:           newMemberShallowFD.UUID,
				ControlAddr:    newMemberShallowFD.Addr,
				FabricURI:      newMemberShallowFD.FabricURI,
				FabricContexts: newMemberShallowFD.FabricContexts,
				FaultDomain:    newMemberShallowFD.FaultDomain,
			},
			expErr: FaultBadFaultDomainDepth(newMemberShallowFD.FaultDomain, curMember.FaultDomain.NumLevels()),
		},
		"update existing member with bad fault domain depth": {
			req: &JoinRequest{
				Rank:           curMember.Rank,
				UUID:           curMember.UUID,
				ControlAddr:    curMember.Addr,
				FabricURI:      curMember.FabricURI,
				FabricContexts: curMember.FabricContexts,
				FaultDomain:    shallowFD,
			},
			expErr: FaultBadFaultDomainDepth(newMemberShallowFD.FaultDomain, curMember.FaultDomain.NumLevels()),
		},
		"change fault domain depth for only member": {
			curMembers: []*Member{
				curMember,
			},
			req: &JoinRequest{
				Rank:        curMember.Rank,
				UUID:        curMember.UUID,
				ControlAddr: curMember.Addr,
				FabricURI:   curMember.Addr.String(),
				FaultDomain: shallowFD,
			},
			expResp: &JoinResponse{
				Member:     MockMember(t, 0, MemberStateJoined).WithFaultDomain(shallowFD),
				PrevState:  curMember.state,
				MapVersion: 2,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms, _ := MockMembership(t, log, mockResolveFn)

			if tc.curMembers == nil {
				tc.curMembers = defaultCurMembers
			}
			for _, curM := range tc.curMembers {
				curM.Rank = NilRank
				if err := ms.addMember(curM); err != nil {
					t.Fatal(err)
				}
			}
			if tc.notLeader {
				_ = ms.db.ShutdownRaft()
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

			ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			defer ps.Close()

			ps.Subscribe(events.RASTypeStateChange, ms)

			ps.Publish(tc.event)

			<-ctx.Done()
			if diff := cmp.Diff(tc.expMembers, ms.Members(nil), memberCmpOpts...); diff != "" {
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
			expErr: &ErrMemberNotFound{byRank: NewRankPtr(42)},
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
	rankDomain := func(parent string, rank uint32) *FaultDomain {
		parentFd := MustCreateFaultDomainFromString(parent)
		member := testMemberWithFaultDomain(t, Rank(rank), parentFd)
		return memberFaultDomain(member)
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
				0,
				expFaultDomainID(0),
				0,
			},
		},
		"single branch, no rank leaves": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomain("one", "two", "three"),
			),
			expResult: []uint32{
				3,
				expFaultDomainID(0),
				1,
				2,
				expFaultDomainID(1),
				1,
				1,
				expFaultDomainID(2),
				1,
				0,
				expFaultDomainID(3),
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
				2, // root
				expFaultDomainID(0),
				3,
				1, // rack0
				expFaultDomainID(1),
				2,
				1, // rack1
				expFaultDomainID(4),
				2,
				1, // rack2
				expFaultDomainID(7),
				1,
				0, // pdu0
				expFaultDomainID(2),
				0,
				0, // pdu1
				expFaultDomainID(3),
				0,
				0, // pdu2
				expFaultDomainID(5),
				0,
				0, // pdu3
				expFaultDomainID(6),
				0,
				0, // pdu4
				expFaultDomainID(8),
				0,
			},
		},
		"single branch with rank leaves": {
			tree: NewFaultDomainTree(
				rankDomain("/one/two/three", 5),
			),
			expResult: []uint32{
				4,
				expFaultDomainID(0),
				1,
				3,
				expFaultDomainID(1),
				1,
				2,
				expFaultDomainID(2),
				1,
				1,
				expFaultDomainID(3),
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
				3,
				expFaultDomainID(0), // root
				3,
				2,
				expFaultDomainID(1), // rack0
				2,
				2,
				expFaultDomainID(6), // rack1
				2,
				2,
				expFaultDomainID(12), // rack2
				1,
				1,
				expFaultDomainID(2), // pdu0
				1,
				1,
				expFaultDomainID(4), // pdu1
				1,
				1,
				expFaultDomainID(7), // pdu2
				1,
				1,
				expFaultDomainID(9), // pdu3
				2,
				1,
				expFaultDomainID(13), // pdu4
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
				rankDomain(fmt.Sprintf("/top/%s2/bottom", rankFaultDomainPrefix), 1),
			),
			expResult: []uint32{
				4,
				expFaultDomainID(0), // root
				1,
				3,
				expFaultDomainID(1), // top
				1,
				2,
				expFaultDomainID(2), // rank2
				1,
				1,
				expFaultDomainID(3), // bottom
				1,
				1, // rank
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
				3,
				expFaultDomainID(0), // root
				1,
				2,
				expFaultDomainID(6), // rack1
				1,
				1,
				expFaultDomainID(9), // pdu3
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
				3,
				expFaultDomainID(0), // root
				3,
				2,
				expFaultDomainID(1), // rack0
				1,
				2,
				expFaultDomainID(6), // rack1
				1,
				2,
				expFaultDomainID(12), // rack2
				1,
				1,
				expFaultDomainID(2), // pdu0
				1,
				1,
				expFaultDomainID(9), // pdu3
				2,
				1,
				expFaultDomainID(13), // pdu4
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
			defer ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)
			db.data.Members.FaultDomains = tc.tree
			membership := NewMembership(log, db)

			result, err := membership.CompressedFaultDomainTree(tc.inputRanks...)

			CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}
