//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// c6xuplcrnless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package system

import (
	"net"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestSystem_Member_Stringify(t *testing.T) {
	states := []MemberState{
		MemberStateUnknown,
		MemberStateAwaitFormat,
		MemberStateStarting,
		MemberStateReady,
		MemberStateJoined,
		MemberStateStopping,
		MemberStateStopped,
		MemberStateEvicted,
		MemberStateErrored,
		MemberStateUnresponsive,
	}

	strs := []string{
		"Unknown",
		"AwaitFormat",
		"Starting",
		"Ready",
		"Joined",
		"Stopping",
		"Stopped",
		"Evicted",
		"Errored",
		"Unresponsive",
	}

	for i, state := range states {
		AssertEqual(t, state.String(), strs[i], strs[i])
	}
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

			ms := NewMembership(log, MockDatabase(t, log))

			if _, err := ms.Add(tc.memberToAdd); err != nil {
				t.Fatal(err)
			}

			m, err := ms.Get(tc.rankToGet)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expMember, m, name)
		})
	}
}

func TestSystem_Membership_AddRemove(t *testing.T) {
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
		"add failure duplicate": {
			Members{
				MockMember(t, 1, MemberStateUnknown),
				MockMember(t, 1, MemberStateUnknown),
			},
			nil,
			nil,
			[]error{nil, &ErrMemberExists{Rank(1)}},
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
			ms := NewMembership(log, MockDatabase(t, log))

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

func TestSystem_Membership_AddOrReplace(t *testing.T) {
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
		"rank uuid and address changed after reformat": {
			Members{&m0a, &m1a, &m2a, &m0b, &m2b, &m1b},
			Members{&m0b, &m2b, &m1b},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := NewMembership(log, MockDatabase(t, log))

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

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(Member{}),
			}
			for _, em := range tc.expMembers {
				m, err := ms.Get(em.Rank)
				if err != nil {
					t.Fatal(err)
				}
				if diff := cmp.Diff(em, m, cmpOpts...); diff != "" {
					t.Fatalf("unexpected member (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestSystem_Membership_HostRanks(t *testing.T) {
	addr1, err := net.ResolveTCPAddr("tcp", "127.0.0.1:10001")
	if err != nil {
		t.Fatal(err)
	}
	members := Members{
		MockMember(t, 1, MemberStateJoined),
		MockMember(t, 2, MemberStateStopped),
		MockMember(t, 3, MemberStateEvicted),
		NewMember(Rank(4), MockUUID(4), addr1.String(), addr1, MemberStateStopped), // second host rank
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

			ms := NewMembership(log, MockDatabase(t, log))

			for _, m := range tc.members {
				if _, err := ms.Add(m); err != nil {
					t.Fatal(err)
				}
			}

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
			AssertEqual(t, tc.expMembers, ms.Members(rankSet), "members")
		})
	}
}

func TestSystem_Membership_CheckRanklist(t *testing.T) {
	addr1, err := net.ResolveTCPAddr("tcp", "127.0.0.1:10001")
	if err != nil {
		t.Fatal(err)
	}
	members := Members{
		MockMember(t, 0, MemberStateJoined),
		MockMember(t, 1, MemberStateJoined),
		MockMember(t, 2, MemberStateStopped),
		MockMember(t, 3, MemberStateEvicted),
		NewMember(Rank(4), common.MockUUID(4), "", addr1, MemberStateStopped), // second host rank
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

			ms := NewMembership(log, MockDatabase(t, log))

			for _, m := range tc.members {
				if _, err := ms.Add(m); err != nil {
					t.Fatal(err)
				}
			}

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
	addr1, err := net.ResolveTCPAddr("tcp", "127.0.0.1:10001")
	if err != nil {
		t.Fatal(err)
	}
	members := Members{
		MockMember(t, 1, MemberStateJoined),
		MockMember(t, 2, MemberStateStopped),
		MockMember(t, 3, MemberStateEvicted),
		MockMember(t, 4, MemberStateJoined),
		MockMember(t, 5, MemberStateJoined),
		NewMember(Rank(6), common.MockUUID(6), "", addr1, MemberStateStopped), // second host rank
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

			ms := NewMembership(log, MockDatabase(t, log))

			for _, m := range tc.members {
				if _, err := ms.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			rankSet, missingHostSet, err := ms.CheckHosts(tc.inHosts, 10001, mockResolveFn)
			CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			AssertEqual(t, tc.expRanks, rankSet.String(), "extant ranks")
			AssertEqual(t, tc.expMissingHosts, missingHostSet.String(), "missing hosts")
		})
	}
}

func TestSystem_Member_Convert(t *testing.T) {
	membersIn := Members{MockMember(t, 1, MemberStateJoined)}
	membersOut := Members{}
	if err := convert.Types(membersIn, &membersOut); err != nil {
		t.Fatal(err)
	}
	AssertEqual(t, membersIn, membersOut, "")
}

func TestSystem_MemberResult_Convert(t *testing.T) {
	mrsIn := MemberResults{
		NewMemberResult(1, nil, MemberStateStopped),
		NewMemberResult(2, errors.New("can't stop"), MemberStateUnknown),
		MockMemberResult(1, "ping", errors.New("foobar"), MemberStateErrored),
	}
	mrsOut := MemberResults{}

	AssertTrue(t, mrsIn.HasErrors(), "")
	AssertFalse(t, mrsOut.HasErrors(), "")

	if err := convert.Types(mrsIn, &mrsOut); err != nil {
		t.Fatal(err)
	}
	AssertEqual(t, mrsIn, mrsOut, "")
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
				MockMember(t, 3, MemberStateEvicted),
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
				MockMember(t, 3, MemberStateEvicted),
				MockMember(t, 4, MemberStateReady),
				MockMember(t, 5, MemberStateJoined), // "Joined" will not be updated to "Ready"
				MockMember(t, 6, MemberStateStopped, "exit 1"),
			},
		},
		"don't ignore errored results": {
			members: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateEvicted),
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
				MockMember(t, 3, MemberStateEvicted),
				MockMember(t, 4, MemberStateReady),
				MockMember(t, 5, MemberStateJoined),
				MockMember(t, 6, MemberStateStopped),
			},
		},
		"errored result with nonerrored state": {
			members: Members{
				MockMember(t, 1, MemberStateJoined),
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateEvicted),
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

			ms := NewMembership(log, MockDatabase(t, log))

			for _, m := range tc.members {
				if _, err := ms.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			// members should be updated with result state
			err := ms.UpdateMemberStates(tc.results, !tc.ignoreErrs)
			ExpectError(t, err, tc.expErrMsg, name)
			if err != nil {
				return
			}

			cmpOpts := []cmp.Option{cmpopts.IgnoreUnexported(MemberResult{}, Member{})}
			for i, m := range ms.Members(nil) {
				if diff := cmp.Diff(tc.expMembers[i], m, cmpOpts...); diff != "" {
					t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
				}
				AssertEqual(t, tc.expMembers[i], m, m.Rank.String())
			}

			// verify result host address is updated to that of member if empty
			for i, r := range tc.expResults {
				AssertEqual(t, tc.expResults[i].Addr, tc.results[i].Addr, r.Rank.String())
			}
		})
	}
}

func TestSystem_Membership_Join(t *testing.T) {
	curMember := MockMember(t, 0, MemberStateJoined)
	newMember := MockMember(t, 1, MemberStateJoined)

	for name, tc := range map[string]struct {
		notLeader bool
		req       *JoinRequest
		expResp   *JoinResponse
		expErr    error
	}{
		"not leader": {
			notLeader: true,
			req:       &JoinRequest{},
			expErr:    &ErrNotLeader{},
		},
		"successful rejoin": {
			req: &JoinRequest{
				Rank:        curMember.Rank,
				UUID:        curMember.UUID,
				ControlAddr: curMember.Addr,
				FabricURI:   curMember.Addr.String(),
			},
			expResp: &JoinResponse{
				Member:     curMember,
				PrevState:  curMember.state,
				MapVersion: 2,
			},
		},
		"rejoin with different rank": {
			req: &JoinRequest{
				Rank:        Rank(42),
				UUID:        curMember.UUID,
				ControlAddr: curMember.Addr,
				FabricURI:   curMember.Addr.String(),
			},
			expErr: errors.New("different rank"),
		},
		"successful join": {
			req: &JoinRequest{
				Rank:           NilRank,
				UUID:           newMember.UUID,
				ControlAddr:    newMember.Addr,
				FabricURI:      newMember.FabricURI,
				FabricContexts: newMember.FabricContexts,
			},
			expResp: &JoinResponse{
				Created:    true,
				Member:     newMember,
				PrevState:  MemberStateUnknown,
				MapVersion: 2,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)
			ms := NewMembership(log, db)
			curMember.Rank = NilRank
			if err := ms.addMember(curMember); err != nil {
				t.Fatal(err)
			}
			if tc.notLeader {
				_ = db.raft.Shutdown().Error()
			}

			gotResp, gotErr := ms.Join(tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(Member{}),
			}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
