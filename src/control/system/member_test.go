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
	"fmt"
	"net"
	"reflect"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

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
			[]error{nil, FaultMemberExists(Rank(1))},
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
			ms := NewMembership(log)

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

			AssertEqual(t, len(tc.expMembers), len(ms.members), name)
		})
	}
}

func assertMembersEqual(t *testing.T, a Member, b Member, msg string) {
	t.Helper()
	AssertTrue(t, reflect.DeepEqual(a, b),
		fmt.Sprintf("%s: want %#v, got %#v", msg, a, b))
}

func TestSystem_Membership_AddOrReplace(t *testing.T) {
	m0a := *MockMember(t, 0, MemberStateStopped)
	m1a := *MockMember(t, 1, MemberStateStopped)
	m2a := *MockMember(t, 2, MemberStateStopped)
	m0b := m0a
	m0b.UUID = "m0b" // uuid changes after reformat
	m0b.state = MemberStateJoined
	m1b := m1a
	m1b.Addr = m0a.Addr // rank allocated differently between hosts after reformat
	m1b.UUID = "m1b"
	m1b.state = MemberStateJoined
	m2b := m2a
	m2a.Addr = m0a.Addr // ranks 0,2 on same host before reformat
	m2b.Addr = m1a.Addr // ranks 0,1 on same host after reformat
	m2b.UUID = "m2b"
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
			Members{&m0b, &m1b, &m2b},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := NewMembership(log)

			for _, m := range tc.membersToAddOrReplace {
				ms.AddOrReplace(m)
			}

			AssertEqual(t, len(tc.expMembers), len(ms.members), name)

			for _, em := range tc.expMembers {
				m, err := ms.Get(em.Rank)
				if err != nil {
					t.Fatal(err)
				}
				assertMembersEqual(t, *em, *m, name)
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
		NewMember(Rank(4), "", addr1, MemberStateStopped), // second host rank
	}

	for name, tc := range map[string]struct {
		members      Members
		rankList     []Rank
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
			rankList: []Rank{1, 2},
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
			rankList:     []Rank{0, 5},
			expRanks:     []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{},
			expHosts:     []string{},
		},
		"superset rank list": {
			members:  members,
			rankList: []Rank{0, 1, 2, 3, 4, 5},
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

			ms := NewMembership(log)

			for _, m := range tc.members {
				if _, err := ms.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			AssertEqual(t, tc.expRanks, ms.Ranks(), "ranks")
			AssertEqual(t, tc.expHostRanks, ms.HostRanks(tc.rankList...), "host ranks")
			AssertEqual(t, tc.expHosts, ms.Hosts(tc.rankList...), "hosts")
			AssertEqual(t, tc.expMembers, ms.Members(tc.rankList...), "members")
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
		NewMember(Rank(4), "", addr1, MemberStateStopped), // second host rank
	}

	for name, tc := range map[string]struct {
		members    Members
		inRanklist string
		expRanks   []Rank
		expMissing []Rank
		expErr     error
	}{
		"no rank list": {
			members: members,
		},
		"bad rank list": {
			members:    members,
			inRanklist: "foobar",
			expErr:     errors.New("unexpected alphabetic character(s)"),
		},
		"no members": {
			inRanklist: "0-4",
			expMissing: []Rank{0, 1, 2, 3, 4},
		},
		"full ranklist": {
			members:    members,
			inRanklist: "0-4",
			expRanks:   []Rank{0, 1, 2, 3, 4},
		},
		"partial ranklist": {
			members:    members,
			inRanklist: "3-4",
			expRanks:   []Rank{3, 4},
		},
		"oversubscribed ranklist": {
			members:    members[3:],
			inRanklist: "0-4",
			expRanks:   []Rank{3, 4},
			expMissing: []Rank{0, 1, 2},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := NewMembership(log)

			for _, m := range tc.members {
				if _, err := ms.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			hit, miss, err := ms.CheckRanklist(tc.inRanklist)
			CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			AssertEqual(t, tc.expRanks, hit, "extant ranks")
			AssertEqual(t, tc.expMissing, miss, "missing ranks")
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

			ms := NewMembership(log)

			for _, m := range tc.members {
				if _, err := ms.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			// members should be updated with result state
			err := ms.UpdateMemberStates(tc.results, tc.ignoreErrs)
			ExpectError(t, err, tc.expErrMsg, name)
			if err != nil {
				return
			}

			cmpOpts := []cmp.Option{cmpopts.IgnoreUnexported(MemberResult{}, Member{})}
			for i, m := range ms.Members() {
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
