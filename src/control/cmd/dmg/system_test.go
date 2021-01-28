//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	. "github.com/daos-stack/daos/src/control/system"
)

func listWithSystem(req *control.ListPoolsReq, sysName string) *control.ListPoolsReq {
	req.SetSystem(sysName)
	return req
}

func TestDmg_SystemCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"system query with no arguments",
			"system query",
			strings.Join([]string{
				printRequest(t, &control.SystemQueryReq{}),
			}, " "),
			nil,
		},
		{
			"system query with single rank",
			"system query --ranks 0",
			strings.Join([]string{
				`*control.SystemQueryReq-{"Sys":"","HostList":null,"Ranks":"0","Hosts":""}`,
			}, " "),
			nil,
		},
		{
			"system query with multiple ranks",
			"system query --ranks 0,2,4-8",
			strings.Join([]string{
				`*control.SystemQueryReq-{"Sys":"","HostList":null,"Ranks":"[0,2,4-8]","Hosts":""}`,
			}, " "),
			nil,
		},
		{
			"system query with bad ranklist",
			"system query --ranks 0,2,four,,4-8",
			"",
			errors.Errorf("creating numeric set from "),
		},
		{
			"system query with single host",
			"system query --rank-hosts foo-0",
			strings.Join([]string{
				`*control.SystemQueryReq-{"Sys":"","HostList":null,"Ranks":"","Hosts":"foo-0"}`,
			}, " "),
			nil,
		},
		{
			"system query with multiple hosts",
			"system query --rank-hosts bar9,foo-[0-100]",
			strings.Join([]string{
				`*control.SystemQueryReq-{"Sys":"","HostList":null,"Ranks":"","Hosts":"bar9,foo-[0-100]"}`,
			}, " "),
			nil,
		},
		{
			"system query with bad hostlist",
			"system query --rank-hosts bar9,foo-[0-100],123",
			"",
			errors.New(`invalid hostname "123"`),
		},
		{
			"system query with both hosts and ranks specified",
			"system query --rank-hosts bar9,foo-[0-100] --ranks 0,2,4-8",
			"",
			errors.New("--ranks and --rank-hosts options cannot be set together"),
		},
		{
			"system query verbose",
			"system query --verbose",
			strings.Join([]string{
				printRequest(t, &control.SystemQueryReq{}),
			}, " "),
			nil,
		},
		{
			"system stop with no arguments",
			"system stop",
			strings.Join([]string{
				printRequest(t, &control.SystemStopReq{
					Prep: true,
					Kill: true,
				}),
			}, " "),
			nil,
		},
		{
			"system stop with force",
			"system stop --force",
			strings.Join([]string{
				printRequest(t, &control.SystemStopReq{
					Prep:  true,
					Kill:  true,
					Force: true,
				}),
			}, " "),
			nil,
		},
		{
			"system stop with single rank",
			"system stop --ranks 0",
			strings.Join([]string{
				`*control.SystemStopReq-{"Sys":"","HostList":null,"Ranks":"0","Hosts":"","Prep":true,"Kill":true,"Force":false}`,
			}, " "),
			nil,
		},
		{
			"system stop with multiple ranks",
			"system stop --ranks 0,1,4",
			strings.Join([]string{
				`*control.SystemStopReq-{"Sys":"","HostList":null,"Ranks":"[0-1,4]","Hosts":"","Prep":true,"Kill":true,"Force":false}`,
			}, " "),
			nil,
		},
		{
			"system stop with multiple hosts",
			"system stop --rank-hosts bar9,foo-[0-100]",
			strings.Join([]string{
				`*control.SystemStopReq-{"Sys":"","HostList":null,"Ranks":"","Hosts":"bar9,foo-[0-100]","Prep":true,"Kill":true,"Force":false}`,
			}, " "),
			nil,
		},
		{
			"system stop with bad hostlist",
			"system stop --rank-hosts bar9,foo-[0-100],123",
			"",
			errors.New(`invalid hostname "123"`),
		},
		{
			"system stop with both hosts and ranks specified",
			"system stop --rank-hosts bar9,foo-[0-100] --ranks 0,2,4-8",
			"",
			errors.New("--ranks and --rank-hosts options cannot be set together"),
		},
		{
			"system start with no arguments",
			"system start",
			strings.Join([]string{
				printRequest(t, &control.SystemStartReq{}),
			}, " "),
			nil,
		},
		{
			"system start with single rank",
			"system start --ranks 0",
			strings.Join([]string{
				`*control.SystemStartReq-{"Sys":"","HostList":null,"Ranks":"0","Hosts":""}`,
			}, " "),
			nil,
		},
		{
			"system start with multiple ranks",
			"system start --ranks 0,1,4",
			strings.Join([]string{
				`*control.SystemStartReq-{"Sys":"","HostList":null,"Ranks":"[0-1,4]","Hosts":""}`,
			}, " "),
			nil,
		},
		{
			"system start with multiple hosts",
			"system start --rank-hosts bar9,foo-[0-100]",
			strings.Join([]string{
				`*control.SystemStartReq-{"Sys":"","HostList":null,"Ranks":"","Hosts":"bar9,foo-[0-100]"}`,
			}, " "),
			nil,
		},
		{
			"system start with bad hostlist",
			"system start --rank-hosts bar9,foo-[0-100],123",
			"",
			errors.New(`invalid hostname "123"`),
		},
		{
			"system start with both hosts and ranks specified",
			"system start --rank-hosts bar9,foo-[0-100] --ranks 0,2,4-8",
			"",
			errors.New("--ranks and --rank-hosts options cannot be set together"),
		},
		{
			"leader query",
			"system leader-query",
			strings.Join([]string{
				printRequest(t, &control.LeaderQueryReq{
					System: build.DefaultSystemName,
				}),
			}, " "),
			nil,
		},
		{
			"system list-pools with default config",
			"system list-pools",
			strings.Join([]string{
				printRequest(t, listWithSystem(&control.ListPoolsReq{}, build.DefaultSystemName)),
			}, " "),
			nil,
		},
		{
			"Non-existent subcommand",
			"system quack",
			"",
			fmt.Errorf("Unknown command"),
		},
		{
			"Non-existent option",
			"system start --rank 0",
			"",
			errors.New("unknown flag `rank'"),
		},
	})
}

func TestDmg_System_rankStateGroups(t *testing.T) {
	for name, tc := range map[string]struct {
		members   Members
		expStates []string
		expOut    string
		expErr    error
	}{
		"nil members": {
			expStates: []string{},
		},
		"no members": {
			members:   Members{},
			expStates: []string{},
		},
		"multiple groups with duplicate": {
			members: Members{
				MockMember(t, 2, MemberStateStopped),
				MockMember(t, 3, MemberStateEvicted),
				MockMember(t, 4, MemberStateEvicted),
				MockMember(t, 4, MemberStateEvicted),
				MockMember(t, 1, MemberStateJoined),
			},
			expErr: &ErrMemberExists{Rank: Rank(4)},
		},
		"multiple groups": {
			members: Members{
				MockMember(t, 1, MemberStateStopped),
				MockMember(t, 3, MemberStateJoined),
				MockMember(t, 5, MemberStateJoined),
				MockMember(t, 4, MemberStateJoined),
				MockMember(t, 8, MemberStateJoined),
				MockMember(t, 2, MemberStateEvicted),
			},
			expStates: []string{
				MemberStateJoined.String(),
				MemberStateEvicted.String(),
				MemberStateStopped.String(),
			},
			expOut: "3-5,8: Joined\n    2: Evicted\n    1: Stopped\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			rsg, gotErr := rankStateGroups(tc.members)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expStates, rsg.Keys()); diff != "" {
				t.Fatalf("unexpected states (-want, +got):\n%s\n", diff)
			}

			if diff := cmp.Diff(tc.expOut, rsg.String()); diff != "" {
				t.Fatalf("unexpected repr (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDmg_System_rankActionGroups(t *testing.T) {
	for name, tc := range map[string]struct {
		results   MemberResults
		expGroups []string
		expOut    string
		expErr    error
	}{
		"nil results": {
			expGroups: []string{},
		},
		"no results": {
			expGroups: []string{},
			results:   MemberResults{},
		},
		"results with no action": {
			results: MemberResults{
				MockMemberResult(4, "ping", nil, MemberStateEvicted),
				MockMemberResult(5, "", nil, MemberStateEvicted),
			},
			expErr: errors.New("action field empty for rank 5 result"),
		},
		"results with duplicate ranks": {
			results: MemberResults{
				MockMemberResult(4, "ping", nil, MemberStateEvicted),
				MockMemberResult(4, "ping", nil, MemberStateEvicted),
			},
			expErr: &ErrMemberExists{Rank: Rank(4)},
		},
		"successful results": {
			results: MemberResults{
				MockMemberResult(2, "ping", nil, MemberStateStopped),
				MockMemberResult(3, "ping", nil, MemberStateEvicted),
				MockMemberResult(4, "ping", nil, MemberStateEvicted),
				MockMemberResult(1, "ping", nil, MemberStateJoined),
			},
			expGroups: []string{"ping/OK"},
			expOut:    "1-4: ping/OK\n",
		},
		"mixed results": {
			results: MemberResults{
				MockMemberResult(2, "ping", nil, MemberStateStopped),
				MockMemberResult(3, "ping", nil, MemberStateEvicted),
				MockMemberResult(4, "ping", errors.New("failure 2"), MemberStateEvicted),
				MockMemberResult(5, "ping", errors.New("failure 2"), MemberStateEvicted),
				MockMemberResult(7, "ping", errors.New("failure 1"), MemberStateEvicted),
				MockMemberResult(6, "ping", errors.New("failure 1"), MemberStateEvicted),
				MockMemberResult(1, "ping", nil, MemberStateJoined),
			},
			expGroups: []string{"ping/OK", "ping/failure 1", "ping/failure 2"},
			expOut:    "1-3: ping/OK\n6-7: ping/failure 1\n4-5: ping/failure 2\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			rag, gotErr := rankActionGroups(tc.results)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expGroups, rag.Keys()); diff != "" {
				t.Fatalf("unexpected groups (-want, +got):\n%s\n", diff)
			}

			if diff := cmp.Diff(tc.expOut, rag.String()); diff != "" {
				t.Fatalf("unexpected repr (-want, +got):\n%s\n", diff)
			}
		})
	}
}
