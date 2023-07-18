//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system_test

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/common/test"
	. "github.com/daos-stack/daos/src/control/system"
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
		MemberStateExcluded,
		MemberStateErrored,
		MemberStateUnresponsive,
		MemberStateAdminExcluded,
	}

	strs := []string{
		"Unknown",
		"AwaitFormat",
		"Starting",
		"Ready",
		"Joined",
		"Stopping",
		"Stopped",
		"Excluded",
		"Errored",
		"Unresponsive",
		"AdminExcluded",
	}

	for i, state := range states {
		test.AssertEqual(t, state.String(), strs[i], strs[i])
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
			test.CmpErr(t, tc.expMarshalErr, err)
			if err != nil {
				return
			}

			unmarshaled := new(Member)
			err = unmarshaled.UnmarshalJSON(marshaled)
			test.CmpErr(t, tc.expUnmarshalErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.member, unmarshaled, cmp.AllowUnexported(Member{})); diff != "" {
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

	if diff := cmp.Diff(membersIn, membersOut, memberCmpOpts...); diff != "" {
		t.Fatalf("unexpected members: (-want, +got)\n%s\n", diff)
	}
}

func TestSystem_MemberResult_Convert(t *testing.T) {
	mrsIn := MemberResults{
		NewMemberResult(1, nil, MemberStateStopped),
		NewMemberResult(2, errors.New("can't stop"), MemberStateUnknown),
		MockMemberResult(1, "ping", errors.New("foobar"), MemberStateErrored),
	}
	mrsOut := MemberResults{}

	test.CmpErr(t, errors.New("failed ranks 1-2"), mrsIn.Errors())
	test.CmpErr(t, nil, mrsOut.Errors())

	if err := convert.Types(mrsIn, &mrsOut); err != nil {
		t.Fatal(err)
	}
	test.AssertEqual(t, mrsIn, mrsOut, "")
}
