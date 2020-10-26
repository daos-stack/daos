//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
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

package control

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	. "github.com/daos-stack/daos/src/control/system"
)

func TestControl_StartRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		uErr    error
		uResps  []*HostResponse
		expResp *RanksResp
		expErr  error
	}{
		"local failure": {
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			uResps: []*HostResponse{
				{
					Addr:  "host1",
					Error: errors.New("remote failed"),
				},
			},
			expResp: &RanksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "remote failed"}),
			},
		},
		"no results": {
			uResps: []*HostResponse{
				{
					Addr:    "host1",
					Message: &mgmtpb.RanksResp{},
				},
			},
			expResp: &RanksResp{},
		},
		"mixed results": {
			uResps: []*HostResponse{
				{
					Addr: "host1",
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Action: "start",
								State: uint32(system.MemberStateReady),
							},
							{
								Rank: 1, Action: "start",
								State: uint32(system.MemberStateReady),
							},
						},
					},
				},
				{
					Addr: "host2",
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, Action: "start",
								State: uint32(system.MemberStateReady),
							},
							{
								Rank: 3, Action: "start",
								Errored: true, Msg: "uh oh",
								State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
				{
					Addr:  "host3",
					Error: errors.New("connection refused"),
				},
			},
			expResp: &RanksResp{
				RankResults: system.MemberResults{
					{Rank: 0, Action: "start", State: system.MemberStateReady},
					{Rank: 1, Action: "start", State: system.MemberStateReady},
					{Rank: 2, Action: "start", State: system.MemberStateReady},
					{Rank: 3, Action: "start", Errored: true, Msg: "uh oh", State: system.MemberStateStopped},
				},
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host3", "connection refused"}),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: &UnaryResponse{Responses: tc.uResps},
			})

			gotResp, gotErr := StartRanks(context.TODO(), mi, &RanksReq{Ranks: "0-3"})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected results (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestControl_PrepShutdownRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		uErr    error
		uResps  []*HostResponse
		expResp *RanksResp
		expErr  error
	}{
		"local failure": {
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			uResps: []*HostResponse{
				{
					Addr:  "host1",
					Error: errors.New("remote failed"),
				},
			},
			expResp: &RanksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "remote failed"}),
			},
		},
		"no results": {
			uResps: []*HostResponse{
				{
					Addr:    "host1",
					Message: &mgmtpb.RanksResp{},
				},
			},
			expResp: &RanksResp{},
		},
		"mixed results": {
			uResps: []*HostResponse{
				{
					Addr: "host1",
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Action: "prep shutdown",
								State: uint32(system.MemberStateStopping),
							},
							{
								Rank: 1, Action: "prep shutdown",
								State: uint32(system.MemberStateStopping),
							},
						},
					},
				},
				{
					Addr: "host2",
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, Action: "prep shutdown",
								State: uint32(system.MemberStateStopping),
							},
							{
								Rank: 3, Action: "prep shutdown",
								Errored: true, Msg: "uh oh",
								State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
				{
					Addr:  "host3",
					Error: errors.New("connection refused"),
				},
			},
			expResp: &RanksResp{
				RankResults: system.MemberResults{
					{Rank: 0, Action: "prep shutdown", State: system.MemberStateStopping},
					{Rank: 1, Action: "prep shutdown", State: system.MemberStateStopping},
					{Rank: 2, Action: "prep shutdown", State: system.MemberStateStopping},
					{Rank: 3, Action: "prep shutdown", Errored: true, Msg: "uh oh", State: system.MemberStateStopped},
				},
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host3", "connection refused"}),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: &UnaryResponse{Responses: tc.uResps},
			})

			gotResp, gotErr := PrepShutdownRanks(context.TODO(), mi, &RanksReq{Ranks: "0-3"})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected results (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestControl_StopRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		uErr    error
		uResps  []*HostResponse
		expResp *RanksResp
		expErr  error
	}{
		"local failure": {
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			uResps: []*HostResponse{
				{
					Addr:  "host1",
					Error: errors.New("remote failed"),
				},
			},
			expResp: &RanksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "remote failed"}),
			},
		},
		"no results": {
			uResps: []*HostResponse{
				{
					Addr:    "host1",
					Message: &mgmtpb.RanksResp{},
				},
			},
			expResp: &RanksResp{},
		},
		"mixed results": {
			uResps: []*HostResponse{
				{
					Addr: "host1",
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Action: "stop",
								State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 1, Action: "stop",
								State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
				{
					Addr: "host2",
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, Action: "stop",
								State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 3, Action: "stop",
								Errored: true, Msg: "uh oh",
								State: uint32(system.MemberStateErrored),
							},
						},
					},
				},
				{
					Addr:  "host3",
					Error: errors.New("connection refused"),
				},
			},
			expResp: &RanksResp{
				RankResults: system.MemberResults{
					{Rank: 0, Action: "stop", State: system.MemberStateStopped},
					{Rank: 1, Action: "stop", State: system.MemberStateStopped},
					{Rank: 2, Action: "stop", State: system.MemberStateStopped},
					{Rank: 3, Action: "stop", Errored: true, Msg: "uh oh", State: system.MemberStateErrored},
				},
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host3", "connection refused"}),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: &UnaryResponse{Responses: tc.uResps},
			})

			gotResp, gotErr := StopRanks(context.TODO(), mi, &RanksReq{Ranks: "0-3", Force: true})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected results (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestControl_PingRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		uErr    error
		uResps  []*HostResponse
		expResp *RanksResp
		expErr  error
	}{
		"local failure": {
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			uResps: []*HostResponse{
				{
					Addr:  "host1",
					Error: errors.New("remote failed"),
				},
			},
			expResp: &RanksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "remote failed"}),
			},
		},
		"no results": {
			uResps: []*HostResponse{
				{
					Addr:    "host1",
					Message: &mgmtpb.RanksResp{},
				},
			},
			expResp: &RanksResp{},
		},
		"mixed results": {
			uResps: []*HostResponse{
				{
					Addr: "host1",
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Action: "ping",
								State: uint32(system.MemberStateReady),
							},
							{
								Rank: 1, Action: "ping",
								State: uint32(system.MemberStateReady),
							},
						},
					},
				},
				{
					Addr: "host2",
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, Action: "ping",
								State: uint32(system.MemberStateReady),
							},
							{
								Rank: 3, Action: "ping",
								Errored: true, Msg: "uh oh",
								State: uint32(system.MemberStateUnresponsive),
							},
						},
					},
				},
				{
					Addr:  "host3",
					Error: errors.New("connection refused"),
				},
			},
			expResp: &RanksResp{
				RankResults: system.MemberResults{
					{Rank: 0, Action: "ping", State: system.MemberStateReady},
					{Rank: 1, Action: "ping", State: system.MemberStateReady},
					{Rank: 2, Action: "ping", State: system.MemberStateReady},
					{Rank: 3, Action: "ping", Errored: true, Msg: "uh oh", State: system.MemberStateUnresponsive},
				},
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host3", "connection refused"}),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: &UnaryResponse{Responses: tc.uResps},
			})

			gotResp, gotErr := PingRanks(context.TODO(), mi, &RanksReq{Ranks: "0-3"})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected results (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestControl_getResetRankErrors(t *testing.T) {
	for name, tc := range map[string]struct {
		results     MemberResults
		expRankErrs map[string][]string
		expHosts    []string
		expErrMsg   string
	}{
		"no results": {
			results:     MemberResults{},
			expRankErrs: make(map[string][]string),
			expHosts:    make([]string, 0),
		},
		"successful result": {
			results: MemberResults{
				{Addr: "10.0.0.1:10001", Rank: Rank(0)},
			},
			expRankErrs: make(map[string][]string),
			expHosts:    []string{"10.0.0.1:10001"},
		},
		"successful result missing address": {
			results: MemberResults{
				{Addr: "", Rank: Rank(0)},
			},
			expErrMsg: "host address missing for rank 0 result",
		},
		"failed result": {
			results: MemberResults{
				{
					Addr: "10.0.0.1:10001", Rank: Rank(0),
					Errored: true, Msg: "didn't start",
				},
			},
			expRankErrs: map[string][]string{"didn't start": {"10.0.0.1:10001"}},
			expHosts:    []string{},
		},
		"failed result missing error message": {
			results: MemberResults{
				{
					Addr: "10.0.0.1:10001", Rank: Rank(0),
					Errored: true, Msg: "",
				},
			},
			expRankErrs: map[string][]string{
				"error message missing for rank result": {"10.0.0.1:10001"},
			},
			expHosts: []string{},
		},
		"mixed results": {
			results: MemberResults{
				{Addr: "10.0.0.1:10001", Rank: Rank(0)},
				{Addr: "10.0.0.1:10001", Rank: Rank(1)},
				{
					Addr: "10.0.0.2:10001", Rank: Rank(2),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.2:10001", Rank: Rank(3),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.3:10001", Rank: Rank(4),
					Errored: true, Msg: "something bad",
				},
				{
					Addr: "10.0.0.3:10001", Rank: Rank(5),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.4:10001", Rank: Rank(6),
					Errored: true, Msg: "something bad",
				},
				{Addr: "10.0.0.4:10001", Rank: Rank(7)},
			},
			expRankErrs: map[string][]string{
				"didn't start": {
					"10.0.0.2:10001", "10.0.0.2:10001", "10.0.0.3:10001",
				},
				"something bad": {"10.0.0.3:10001", "10.0.0.4:10001"},
			},
			expHosts: []string{"10.0.0.1:10001", "10.0.0.4:10001"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			rankErrs, hosts, err := getResetRankErrors(tc.results)
			common.ExpectError(t, err, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			common.AssertEqual(t, tc.expRankErrs, rankErrs, name)
			common.AssertStringsEqual(t, tc.expHosts, hosts, "host list")
		})
	}
}

func TestControl_SystemQuery(t *testing.T) {
	testHS := hostlist.MustCreateSet("foo-[1-23]")
	testReqHS := new(SystemQueryReq)
	testReqHS.Hosts.ReplaceSet(testHS)
	testRespHS := new(SystemQueryResp)
	testRespHS.AbsentHosts.ReplaceSet(testHS)

	testRS := system.MustCreateRankSet("1-23")
	testReqRS := new(SystemQueryReq)
	testReqRS.Ranks.ReplaceSet(testRS)
	testRespRS := new(SystemQueryResp)
	testRespRS.AbsentRanks.ReplaceSet(testRS)

	for name, tc := range map[string]struct {
		req     *SystemQueryReq
		uErr    error
		uResp   *UnaryResponse
		expResp *SystemQueryResp
		expErr  error
	}{
		"nil req": {
			req:    nil,
			expErr: errors.New("nil *control.SystemQueryReq request"),
		},
		"local failure": {
			req:    new(SystemQueryReq),
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req:    new(SystemQueryReq),
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"request absent host set": {
			req: testReqHS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&ctlpb.SystemQueryResp{
					Absenthosts: "foo-[1-23]",
				}),
			expResp: testRespHS,
		},
		"request absent rank set": {
			req: testReqRS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&ctlpb.SystemQueryResp{
					Absentranks: "1-23",
				}),
			expResp: testRespRS,
		},
		"dual host dual rank": {
			req: new(SystemQueryReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&ctlpb.SystemQueryResp{
					Members: []*ctlpb.SystemMember{
						{
							Rank:  1,
							Uuid:  common.MockUUID(1),
							State: uint32(MemberStateReady),
							Addr:  "10.0.0.1:10001",
						},
						{
							Rank:  2,
							Uuid:  common.MockUUID(2),
							State: uint32(MemberStateReady),
							Addr:  "10.0.0.1:10001",
						},
						{
							Rank:  0,
							Uuid:  common.MockUUID(0),
							State: uint32(MemberStateStopped),
							Addr:  "10.0.0.2:10001",
						},
						{
							Rank:  3,
							Uuid:  common.MockUUID(3),
							State: uint32(MemberStateStopped),
							Addr:  "10.0.0.2:10001",
						},
					},
				},
			),
			expResp: &SystemQueryResp{
				Members: system.Members{
					system.NewMember(1, common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateReady),
					system.NewMember(2, common.MockUUID(2), "", common.MockHostAddr(1), system.MemberStateReady),
					system.NewMember(0, common.MockUUID(0), "", common.MockHostAddr(2), system.MemberStateStopped),
					system.NewMember(3, common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemQuery(context.TODO(), mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{cmpopts.IgnoreUnexported(SystemQueryResp{}, system.Member{})}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResp.AbsentHosts.String(), gotResp.AbsentHosts.String()); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResp.AbsentRanks.String(), gotResp.AbsentRanks.String()); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_SystemStart(t *testing.T) {
	testHS := hostlist.MustCreateSet("foo-[1-23]")
	testReqHS := new(SystemStartReq)
	testReqHS.Hosts.ReplaceSet(testHS)
	testRespHS := new(SystemStartResp)
	testRespHS.AbsentHosts.ReplaceSet(testHS)

	testRS := system.MustCreateRankSet("1-23")
	testReqRS := new(SystemStartReq)
	testReqRS.Ranks.ReplaceSet(testRS)
	testRespRS := new(SystemStartResp)
	testRespRS.AbsentRanks.ReplaceSet(testRS)

	for name, tc := range map[string]struct {
		req     *SystemStartReq
		uErr    error
		uResp   *UnaryResponse
		expResp *SystemStartResp
		expErr  error
	}{
		"nil req": {
			req:    nil,
			expErr: errors.New("nil *control.SystemStartReq request"),
		},
		"local failure": {
			req:    new(SystemStartReq),
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req:    new(SystemStartReq),
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"request absent host set": {
			req: testReqHS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&ctlpb.SystemStartResp{
					Absenthosts: "foo-[1-23]",
				}),
			expResp: testRespHS,
		},
		"request absent rank set": {
			req: testReqRS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&ctlpb.SystemStartResp{
					Absentranks: "1-23",
				}),
			expResp: testRespRS,
		},
		"dual host dual rank": {
			req: new(SystemStartReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&ctlpb.SystemStartResp{
					Results: []*ctlpb.RankResult{
						{
							Rank:  1,
							State: uint32(MemberStateReady),
						},
						{
							Rank:  2,
							State: uint32(MemberStateReady),
						},
						{
							Rank:  0,
							State: uint32(MemberStateStopped),
						},
						{
							Rank:  3,
							State: uint32(MemberStateStopped),
						},
					},
				},
			),
			expResp: &SystemStartResp{
				Results: system.MemberResults{
					system.NewMemberResult(1, nil, system.MemberStateReady),
					system.NewMemberResult(2, nil, system.MemberStateReady),
					system.NewMemberResult(0, nil, system.MemberStateStopped),
					system.NewMemberResult(3, nil, system.MemberStateStopped),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemStart(context.TODO(), mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{cmpopts.IgnoreUnexported(SystemStartResp{}, system.MemberResult{})}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResp.AbsentHosts.String(), gotResp.AbsentHosts.String()); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResp.AbsentRanks.String(), gotResp.AbsentRanks.String()); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_SystemStop(t *testing.T) {
	testHS := hostlist.MustCreateSet("foo-[1-23]")
	testReqHS := new(SystemStopReq)
	testReqHS.Hosts.ReplaceSet(testHS)
	testRespHS := new(SystemStopResp)
	testRespHS.AbsentHosts.ReplaceSet(testHS)

	testRS := system.MustCreateRankSet("1-23")
	testReqRS := new(SystemStopReq)
	testReqRS.Ranks.ReplaceSet(testRS)
	testRespRS := new(SystemStopResp)
	testRespRS.AbsentRanks.ReplaceSet(testRS)

	for name, tc := range map[string]struct {
		req     *SystemStopReq
		uErr    error
		uResp   *UnaryResponse
		expResp *SystemStopResp
		expErr  error
	}{
		"nil req": {
			req:    nil,
			expErr: errors.New("nil *control.SystemStopReq request"),
		},
		"local failure": {
			req:    new(SystemStopReq),
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req:    new(SystemStopReq),
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"request absent host set": {
			req: testReqHS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&ctlpb.SystemStopResp{
					Absenthosts: "foo-[1-23]",
				}),
			expResp: testRespHS,
		},
		"request absent rank set": {
			req: testReqRS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&ctlpb.SystemStopResp{
					Absentranks: "1-23",
				}),
			expResp: testRespRS,
		},
		"dual host dual rank": {
			req: new(SystemStopReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&ctlpb.SystemStopResp{
					Results: []*ctlpb.RankResult{
						{
							Rank:  1,
							State: uint32(MemberStateReady),
						},
						{
							Rank:  2,
							State: uint32(MemberStateReady),
						},
						{
							Rank:  0,
							State: uint32(MemberStateStopped),
						},
						{
							Rank:  3,
							State: uint32(MemberStateStopped),
						},
					},
				},
			),
			expResp: &SystemStopResp{
				Results: system.MemberResults{
					system.NewMemberResult(1, nil, system.MemberStateReady),
					system.NewMemberResult(2, nil, system.MemberStateReady),
					system.NewMemberResult(0, nil, system.MemberStateStopped),
					system.NewMemberResult(3, nil, system.MemberStateStopped),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemStop(context.TODO(), mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{cmpopts.IgnoreUnexported(SystemStopResp{}, system.MemberResult{})}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResp.AbsentHosts.String(), gotResp.AbsentHosts.String()); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResp.AbsentRanks.String(), gotResp.AbsentRanks.String()); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_SystemReformat(t *testing.T) {
	for name, tc := range map[string]struct {
		req     *SystemResetFormatReq
		uErr    error
		uResp   *UnaryResponse
		expResp *StorageFormatResp
		expErr  error
	}{
		"nil req": {
			req:    nil,
			expErr: errors.New("nil *control.SystemResetFormatReq request"),
		},
		"local failure": {
			req:    new(SystemResetFormatReq),
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req:    new(SystemResetFormatReq),
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"no results": {
			req: new(SystemResetFormatReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&ctlpb.SystemResetFormatResp{
					Results: []*ctlpb.RankResult{},
				},
			),
			// indicates SystemReformat internally proceeded to invoke StorageFormat RPCs
			// which is expected when no host errors are returned
			expErr: errors.New("unpack"),
		},
		"single host dual rank": {
			req: new(SystemResetFormatReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&ctlpb.SystemResetFormatResp{
					Results: []*ctlpb.RankResult{
						{
							Rank: 1, Action: "reset format",
							State: uint32(MemberStateAwaitFormat),
							Addr:  "10.0.0.1:10001",
						},
						{
							Rank: 2, Action: "reset format",
							State: uint32(MemberStateAwaitFormat),
							Addr:  "10.0.0.1:10001",
						},
					},
				},
			),
			// indicates SystemReformat internally proceeded to invoke StorageFormat RPCs
			// which is expected when no host errors are returned
			expErr: errors.New("unpack"),
		},
		"single host dual rank one failed": {
			req: new(SystemResetFormatReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&ctlpb.SystemResetFormatResp{
					Results: []*ctlpb.RankResult{
						{
							Rank: 1, Action: "reset format",
							State:   uint32(MemberStateStopped),
							Addr:    "10.0.0.1:10001",
							Errored: true, Msg: "didn't start",
						},
						{
							Rank: 2, Action: "reset format",
							State: uint32(MemberStateAwaitFormat),
							Addr:  "10.0.0.1:10001",
						},
					},
				},
			),
			expResp: &StorageFormatResp{
				HostErrorsResp: HostErrorsResp{
					HostErrors: mockHostErrorsMap(t, &MockHostError{
						"10.0.0.1:10001", "1 rank failed: didn't start",
					}),
				},
			},
		},
		"multiple hosts dual rank mixed results": {
			req: new(SystemResetFormatReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&ctlpb.SystemResetFormatResp{
					Results: []*ctlpb.RankResult{
						{
							Rank: 1, Action: "reset format",
							State:   uint32(MemberStateStopped),
							Addr:    "10.0.0.1:10001",
							Errored: true, Msg: "didn't start",
						},
						{
							Rank: 2, Action: "reset format",
							State: uint32(MemberStateAwaitFormat),
							Addr:  "10.0.0.1:10001",
						},
						{
							Rank: 3, Action: "reset format",
							State: uint32(MemberStateAwaitFormat),
							Addr:  "10.0.0.2:10001",
						},
						{
							Rank: 4, Action: "reset format",
							State: uint32(MemberStateAwaitFormat),
							Addr:  "10.0.0.2:10001",
						},
						{
							Rank: 5, Action: "reset format",
							State:   uint32(MemberStateStopped),
							Addr:    "10.0.0.3:10001",
							Errored: true, Msg: "didn't start",
						},
						{
							Rank: 6, Action: "reset format",
							State:   uint32(MemberStateErrored),
							Addr:    "10.0.0.3:10001",
							Errored: true, Msg: "something bad",
						},
						{
							Rank: 7, Action: "reset format",
							State:   uint32(MemberStateStopped),
							Addr:    "10.0.0.4:10001",
							Errored: true, Msg: "didn't start",
						},
						{
							Rank: 8, Action: "reset format",
							State:   uint32(MemberStateErrored),
							Addr:    "10.0.0.4:10001",
							Errored: true, Msg: "didn't start",
						},
					},
				},
			),
			expResp: &StorageFormatResp{
				HostErrorsResp: HostErrorsResp{
					HostErrors: mockHostErrorsMap(t,
						&MockHostError{"10.0.0.4:10001", "2 ranks failed: didn't start"},
						&MockHostError{"10.0.0.[1,3]:10001", "1 rank failed: didn't start"},
						&MockHostError{"10.0.0.3:10001", "1 rank failed: something bad"},
					),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemReformat(context.TODO(), mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
