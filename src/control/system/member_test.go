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
	"testing"

	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/logging"
)

func mockMember(t *testing.T, idx uint32, state MemberState) *Member {
	addr, err := net.ResolveTCPAddr("tcp",
		fmt.Sprintf("127.0.0.%d:10001", idx))
	if err != nil {
		t.Fatal(err)
	}

	return NewMember(Rank(idx), fmt.Sprintf("abcd-efgh-ijkl-mno%d", idx),
		addr, state)
}

func TestMember_Stringify(t *testing.T) {
	states := []MemberState{
		MemberStateUnknown,
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

func TestMember_AddRemove(t *testing.T) {
	for name, tc := range map[string]struct {
		membersToAdd  Members
		ranksToRemove []Rank
		expMembers    Members
		expAddErrs    []error
	}{
		"add remove success": {
			Members{
				mockMember(t, 1, MemberStateUnknown),
				mockMember(t, 2, MemberStateUnknown),
			},
			[]Rank{1, 2},
			Members{},
			[]error{nil, nil},
		},
		"add failure duplicate": {
			Members{
				mockMember(t, 1, MemberStateUnknown),
				mockMember(t, 1, MemberStateUnknown),
			},
			nil,
			nil,
			[]error{nil, FaultMemberExists},
		},
		"remove non-existent": {
			Members{
				mockMember(t, 1, MemberStateUnknown),
				mockMember(t, 2, MemberStateUnknown),
			},
			[]Rank{3},
			Members{
				mockMember(t, 1, MemberStateUnknown),
				mockMember(t, 2, MemberStateUnknown),
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

func TestMember_AddOrUpdate(t *testing.T) {
	started := MemberStateJoined

	for name, tc := range map[string]struct {
		membersToAddOrUpdate Members
		expMembers           Members
		expCreated           []bool
		expOldState          []*MemberState
	}{
		"add then update": {
			Members{
				mockMember(t, 1, MemberStateJoined),
				mockMember(t, 1, MemberStateStopped),
			},
			Members{mockMember(t, 1, MemberStateStopped)},
			[]bool{true, false},
			[]*MemberState{nil, &started},
		},
		"add multiple": {
			Members{
				mockMember(t, 1, MemberStateUnknown),
				mockMember(t, 2, MemberStateUnknown),
			},
			Members{
				mockMember(t, 1, MemberStateUnknown),
				mockMember(t, 2, MemberStateUnknown),
			},
			[]bool{true, true},
			[]*MemberState{nil, nil},
		},
		"update same state": {
			Members{
				mockMember(t, 1, MemberStateJoined),
				mockMember(t, 1, MemberStateJoined),
			},
			Members{mockMember(t, 1, MemberStateJoined)},
			[]bool{true, false},
			[]*MemberState{nil, &started},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			ms := NewMembership(log)

			for i, m := range tc.membersToAddOrUpdate {
				created, oldState := ms.AddOrUpdate(m)
				AssertEqual(t, tc.expCreated[i], created, name)
				AssertEqual(t, tc.expOldState[i], oldState, name)

			}

			AssertEqual(t, len(tc.expMembers), len(ms.members), name)

			for _, em := range tc.expMembers {
				m, err := ms.Get(em.Rank)
				if err != nil {
					t.Fatal(err)
				}
				AssertEqual(t, em.Rank, m.Rank, name)
				AssertEqual(t, em.State(), m.State(), name)

				// verify UpdateState works as expected
				if err := ms.SetMemberState(m.Rank, MemberStateEvicted); err != nil {
					t.Fatal(err)
				}

				m, err = ms.Get(em.Rank)
				if err != nil {
					t.Fatal(err)
				}
				AssertEqual(t, em.Rank, m.Rank, name)
				AssertEqual(t, MemberStateEvicted, m.State(), name)
			}
		})
	}
}

func TestMember_HostRanks(t *testing.T) {
	addr1, err := net.ResolveTCPAddr("tcp", "127.0.0.1:10001")
	if err != nil {
		t.Fatal(err)
	}
	members := Members{
		mockMember(t, 1, MemberStateJoined),
		mockMember(t, 2, MemberStateStopped),
		mockMember(t, 3, MemberStateEvicted),
		NewMember(Rank(4), "", addr1, MemberStateStopped), // second host rank
	}

	for name, tc := range map[string]struct {
		members      Members
		rankList     []Rank
		expRanks     []Rank
		expHostRanks map[string][]Rank
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
			expMembers: Members{
				mockMember(t, 1, MemberStateJoined),
				mockMember(t, 2, MemberStateStopped),
			},
		},
		"distinct rank list": {
			members:      members,
			rankList:     []Rank{0, 5},
			expRanks:     []Rank{1, 2, 3, 4},
			expHostRanks: map[string][]Rank{},
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
			AssertEqual(t, tc.expMembers, ms.Members(tc.rankList), "members")
		})
	}
}

func TestMember_Convert(t *testing.T) {
	membersIn := Members{mockMember(t, 1, MemberStateJoined)}
	membersOut := Members{}
	if err := convert.Types(membersIn, &membersOut); err != nil {
		t.Fatal(err)
	}
	AssertEqual(t, membersIn, membersOut, "")
}

func TestMemberResult_Convert(t *testing.T) {
	mrsIn := MemberResults{
		NewMemberResult(1, "query", nil, MemberStateStopped),
		NewMemberResult(2, "stop", errors.New("can't stop"), MemberStateUnknown),
	}
	mrsOut := MemberResults{}

	AssertTrue(t, mrsIn.HasErrors(), "")
	AssertFalse(t, mrsOut.HasErrors(), "")

	if err := convert.Types(mrsIn, &mrsOut); err != nil {
		t.Fatal(err)
	}
	AssertEqual(t, mrsIn, mrsOut, "")
}

func TestMember_UpdateMemberStates(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	ms := NewMembership(log)

	members := Members{
		mockMember(t, 1, MemberStateJoined),
		mockMember(t, 2, MemberStateStopped),
		mockMember(t, 3, MemberStateEvicted),
		mockMember(t, 4, MemberStateStopped),
	}
	results := MemberResults{
		NewMemberResult(1, "query", nil, MemberStateStopped),
		NewMemberResult(2, "stop", errors.New("can't stop"), MemberStateErrored),
		NewMemberResult(4, "start", nil, MemberStateJoined),
	}
	expStates := []MemberState{
		MemberStateStopped,
		MemberStateErrored,
		MemberStateEvicted,
		MemberStateJoined,
	}

	for _, m := range members {
		if _, err := ms.Add(m); err != nil {
			t.Fatal(err)
		}
	}

	// members should be updated with result state
	if err := ms.UpdateMemberStates(results); err != nil {
		t.Fatal(err)
	}

	for i, m := range ms.Members([]Rank{}) {
		AssertEqual(t, expStates[i], m.State(), "")
	}
}
