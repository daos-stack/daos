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
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
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

func TestSystem_Member_MarshalUnmarshalJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		member          *Member
		expMarshalErr   error
		expUnmarshalErr error
	}{
		"nil": {
			expMarshalErr: errors.New("tried to marshal nil Member"),
		},
		"empty": {
			member:          &Member{},
			expUnmarshalErr: errors.New("address <nil>: missing port in address"),
		},
		"success": {
			member: MockMember(t, 1, MemberStateReady),
		},
		"with info": {
			member: MockMember(t, 2, MemberStateJoined, "info"),
		},
		"with fault domain": {
			member: MockMember(t, 3, MemberStateStopped, "info").
				WithFaultDomain(MustCreateFaultDomainFromString("/test/fault/domain")),
		},
	} {
		t.Run(name, func(t *testing.T) {
			marshaled, err := tc.member.MarshalJSON()
			common.CmpErr(t, tc.expMarshalErr, err)
			if err != nil {
				return
			}

			unmarshaled := new(Member)
			err = unmarshaled.UnmarshalJSON(marshaled)
			common.CmpErr(t, tc.expUnmarshalErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.member, unmarshaled, cmp.AllowUnexported(Member{})); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_Member_RankFaultDomain(t *testing.T) {
	for name, tc := range map[string]struct {
		rank        Rank
		faultDomain *FaultDomain
		expResult   *FaultDomain
	}{
		"nil fault domain": {
			expResult: MustCreateFaultDomain("rank0"),
		},
		"empty fault domain": {
			rank:        Rank(2),
			faultDomain: MustCreateFaultDomain(),
			expResult:   MustCreateFaultDomain("rank2"),
		},
		"existing fault domain": {
			rank:        Rank(1),
			faultDomain: MustCreateFaultDomain("one", "two"),
			expResult:   MustCreateFaultDomain("one", "two", "rank1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			m := NewMember(tc.rank, uuid.New().String(), "dontcare", &net.TCPAddr{}, MemberStateJoined).
				WithFaultDomain(tc.faultDomain)

			result := m.RankFaultDomain()

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
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
