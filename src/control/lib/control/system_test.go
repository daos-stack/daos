//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"log"
	"log/syslog"
	"math"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
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
					Message: &mgmtpb.SystemStartResp{},
				},
			},
			expResp: &RanksResp{},
		},
		"mixed results": {
			uResps: []*HostResponse{
				{
					Addr: "host1",
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 0, Action: "start",
								State: system.MemberStateReady.String(),
							},
							{
								Rank: 1, Action: "start",
								State: system.MemberStateReady.String(),
							},
						},
					},
				},
				{
					Addr: "host2",
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 2, Action: "start",
								State: system.MemberStateReady.String(),
							},
							{
								Rank: 3, Action: "start",
								Errored: true, Msg: "uh oh",
								State: system.MemberStateStopped.String(),
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
					Message: &mgmtpb.SystemStartResp{},
				},
			},
			expResp: &RanksResp{},
		},
		"mixed results": {
			uResps: []*HostResponse{
				{
					Addr: "host1",
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 0, Action: "prep shutdown",
								State: system.MemberStateStopping.String(),
							},
							{
								Rank: 1, Action: "prep shutdown",
								State: system.MemberStateStopping.String(),
							},
						},
					},
				},
				{
					Addr: "host2",
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 2, Action: "prep shutdown",
								State: system.MemberStateStopping.String(),
							},
							{
								Rank: 3, Action: "prep shutdown",
								Errored: true, Msg: "uh oh",
								State: system.MemberStateStopped.String(),
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
					Message: &mgmtpb.SystemStartResp{},
				},
			},
			expResp: &RanksResp{},
		},
		"mixed results": {
			uResps: []*HostResponse{
				{
					Addr: "host1",
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 0, Action: "stop",
								State: system.MemberStateStopped.String(),
							},
							{
								Rank: 1, Action: "stop",
								State: system.MemberStateStopped.String(),
							},
						},
					},
				},
				{
					Addr: "host2",
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 2, Action: "stop",
								State: system.MemberStateStopped.String(),
							},
							{
								Rank: 3, Action: "stop",
								Errored: true, Msg: "uh oh",
								State: system.MemberStateErrored.String(),
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
					Message: &ctlpb.RanksResp{},
				},
			},
			expResp: &RanksResp{},
		},
		"mixed results": {
			uResps: []*HostResponse{
				{
					Addr: "host1",
					Message: &ctlpb.RanksResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 0, Action: "ping",
								State: system.MemberStateReady.String(),
							},
							{
								Rank: 1, Action: "ping",
								State: system.MemberStateReady.String(),
							},
						},
					},
				},
				{
					Addr: "host2",
					Message: &ctlpb.RanksResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 2, Action: "ping",
								State: system.MemberStateReady.String(),
							},
							{
								Rank: 3, Action: "ping",
								Errored: true, Msg: "uh oh",
								State: system.MemberStateUnresponsive.String(),
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
		results     system.MemberResults
		expRankErrs map[string][]string
		expHosts    []string
		expErrMsg   string
	}{
		"no results": {
			results:     system.MemberResults{},
			expRankErrs: make(map[string][]string),
			expHosts:    make([]string, 0),
		},
		"successful result": {
			results: system.MemberResults{
				{Addr: "10.0.0.1:10001", Rank: system.Rank(0)},
			},
			expRankErrs: make(map[string][]string),
			expHosts:    []string{"10.0.0.1:10001"},
		},
		"successful result missing address": {
			results: system.MemberResults{
				{Addr: "", Rank: system.Rank(0)},
			},
			expErrMsg: "host address missing for rank 0 result",
		},
		"failed result": {
			results: system.MemberResults{
				{
					Addr: "10.0.0.1:10001", Rank: system.Rank(0),
					Errored: true, Msg: "didn't start",
				},
			},
			expRankErrs: map[string][]string{"didn't start": {"10.0.0.1:10001"}},
			expHosts:    []string{},
		},
		"failed result missing error message": {
			results: system.MemberResults{
				{
					Addr: "10.0.0.1:10001", Rank: system.Rank(0),
					Errored: true, Msg: "",
				},
			},
			expRankErrs: map[string][]string{
				"error message missing for rank result": {"10.0.0.1:10001"},
			},
			expHosts: []string{},
		},
		"mixed results": {
			results: system.MemberResults{
				{Addr: "10.0.0.1:10001", Rank: system.Rank(0)},
				{Addr: "10.0.0.1:10001", Rank: system.Rank(1)},
				{
					Addr: "10.0.0.2:10001", Rank: system.Rank(2),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.2:10001", Rank: system.Rank(3),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.3:10001", Rank: system.Rank(4),
					Errored: true, Msg: "something bad",
				},
				{
					Addr: "10.0.0.3:10001", Rank: system.Rank(5),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.4:10001", Rank: system.Rank(6),
					Errored: true, Msg: "something bad",
				},
				{Addr: "10.0.0.4:10001", Rank: system.Rank(7)},
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

	fdStrs := []string{"/one/two", "/three", "/four/five/six", ""}
	fds := make([]*system.FaultDomain, len(fdStrs))
	for i := range fdStrs {
		fds[i] = system.MustCreateFaultDomainFromString(fdStrs[i])
	}

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
				&mgmtpb.SystemQueryResp{
					Absenthosts: "foo-[1-23]",
				}),
			expResp: testRespHS,
		},
		"request absent rank set": {
			req: testReqRS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&mgmtpb.SystemQueryResp{
					Absentranks: "1-23",
				}),
			expResp: testRespRS,
		},
		"dual host dual rank": {
			req: new(SystemQueryReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&mgmtpb.SystemQueryResp{
					Members: []*mgmtpb.SystemMember{
						{
							Rank:        1,
							Uuid:        common.MockUUID(1),
							State:       system.MemberStateReady.String(),
							Addr:        "10.0.0.1:10001",
							FaultDomain: fdStrs[1],
						},
						{
							Rank:        2,
							Uuid:        common.MockUUID(2),
							State:       system.MemberStateReady.String(),
							Addr:        "10.0.0.1:10001",
							FaultDomain: fdStrs[2],
						},
						{
							Rank:        0,
							Uuid:        common.MockUUID(0),
							State:       system.MemberStateStopped.String(),
							Addr:        "10.0.0.2:10001",
							FaultDomain: fdStrs[0],
						},
						{
							Rank:        3,
							Uuid:        common.MockUUID(3),
							State:       system.MemberStateStopped.String(),
							Addr:        "10.0.0.2:10001",
							FaultDomain: fdStrs[3],
						},
					},
				},
			),
			expResp: &SystemQueryResp{
				Members: system.Members{
					system.NewMember(1, common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateReady).WithFaultDomain(fds[1]),
					system.NewMember(2, common.MockUUID(2), "", common.MockHostAddr(1), system.MemberStateReady).WithFaultDomain(fds[2]),
					system.NewMember(0, common.MockUUID(0), "", common.MockHostAddr(2), system.MemberStateStopped).WithFaultDomain(fds[0]),
					system.NewMember(3, common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped).WithFaultDomain(fds[3]),
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

func TestControl_SystemQueryRespErrors(t *testing.T) {
	for name, tc := range map[string]struct {
		absentHosts string
		absentRanks string
		expErr      error
	}{
		"no errors": {},
		"absent hosts": {
			absentHosts: "foo-[1-23]",
			expErr:      errors.New("non-existent hosts foo-[1-23]"),
		},
		"absent ranks": {
			absentRanks: "1-23",
			expErr:      errors.New("non-existent ranks 1-23"),
		},
		"both absent hosts and ranks": {
			absentHosts: "foo-[1-23]",
			absentRanks: "1-23",
			expErr:      errors.New("non-existent hosts foo-[1-23], non-existent ranks 1-23"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			resp := new(SystemQueryResp)
			ahs := hostlist.MustCreateSet(tc.absentHosts)
			resp.AbsentHosts.ReplaceSet(ahs)
			ars := system.MustCreateRankSet(tc.absentRanks)
			resp.AbsentRanks.ReplaceSet(ars)

			common.CmpErr(t, tc.expErr, resp.Errors())
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
				&mgmtpb.SystemStartResp{
					Absenthosts: "foo-[1-23]",
				}),
			expResp: testRespHS,
		},
		"request absent rank set": {
			req: testReqRS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&mgmtpb.SystemStartResp{
					Absentranks: "1-23",
				}),
			expResp: testRespRS,
		},
		"dual host dual rank": {
			req: new(SystemStartReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&mgmtpb.SystemStartResp{
					Results: []*sharedpb.RankResult{
						{
							Rank:  1,
							State: system.MemberStateReady.String(),
						},
						{
							Rank:  2,
							State: system.MemberStateReady.String(),
						},
						{
							Rank:  0,
							State: system.MemberStateStopped.String(),
						},
						{
							Rank:  3,
							State: system.MemberStateStopped.String(),
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

func TestControl_SystemStartRespErrors(t *testing.T) {
	successResults := system.MemberResults{
		system.NewMemberResult(1, nil, system.MemberStateReady),
		system.NewMemberResult(2, nil, system.MemberStateReady),
		system.NewMemberResult(0, nil, system.MemberStateStopped),
		system.NewMemberResult(3, nil, system.MemberStateStopped),
	}
	failedResults := system.MemberResults{
		system.NewMemberResult(1, nil, system.MemberStateReady),
		system.NewMemberResult(2, errors.New("fail"), system.MemberStateReady),
		system.NewMemberResult(0, errors.New("failed"), system.MemberStateStopped),
		system.NewMemberResult(3, nil, system.MemberStateStopped),
	}

	for name, tc := range map[string]struct {
		absentHosts string
		absentRanks string
		results     system.MemberResults
		expErr      error
	}{
		"no errors": {
			results: successResults,
		},
		"absent hosts": {
			absentHosts: "foo-[1-23]",
			results:     successResults,
			expErr:      errors.New("non-existent hosts foo-[1-23]"),
		},
		"absent ranks": {
			absentRanks: "1-23",
			results:     successResults,
			expErr:      errors.New("non-existent ranks 1-23"),
		},
		"failed ranks": {
			results: failedResults,
			expErr:  errors.New("check results for failed ranks 0,2"),
		},
		"absent hosts and ranks with failed ranks": {
			absentHosts: "foo-[1-23]",
			absentRanks: "1-23",
			results:     failedResults,
			expErr: errors.New("non-existent hosts foo-[1-23], " +
				"non-existent ranks 1-23, check results for failed ranks 0,2"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			resp := new(SystemStartResp)
			ahs := hostlist.MustCreateSet(tc.absentHosts)
			resp.AbsentHosts.ReplaceSet(ahs)
			ars := system.MustCreateRankSet(tc.absentRanks)
			resp.AbsentRanks.ReplaceSet(ars)
			resp.Results = tc.results

			common.CmpErr(t, tc.expErr, resp.Errors())
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
				&mgmtpb.SystemStopResp{
					Absenthosts: "foo-[1-23]",
				}),
			expResp: testRespHS,
		},
		"request absent rank set": {
			req: testReqRS,
			uResp: MockMSResponse("0.0.0.0", nil,
				&mgmtpb.SystemStopResp{
					Absentranks: "1-23",
				}),
			expResp: testRespRS,
		},
		"dual host dual rank": {
			req: new(SystemStopReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&mgmtpb.SystemStopResp{
					Results: []*sharedpb.RankResult{
						{
							Rank:  1,
							State: system.MemberStateReady.String(),
						},
						{
							Rank:  2,
							State: system.MemberStateReady.String(),
						},
						{
							Rank:  0,
							State: system.MemberStateStopped.String(),
						},
						{
							Rank:  3,
							State: system.MemberStateStopped.String(),
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

func TestControl_SystemStopRespErrors(t *testing.T) {
	successResults := system.MemberResults{
		system.NewMemberResult(1, nil, system.MemberStateReady),
		system.NewMemberResult(2, nil, system.MemberStateReady),
		system.NewMemberResult(0, nil, system.MemberStateStopped),
		system.NewMemberResult(3, nil, system.MemberStateStopped),
	}
	failedResults := system.MemberResults{
		system.NewMemberResult(1, nil, system.MemberStateReady),
		system.NewMemberResult(2, errors.New("fail"), system.MemberStateReady),
		system.NewMemberResult(0, errors.New("failed"), system.MemberStateStopped),
		system.NewMemberResult(3, nil, system.MemberStateStopped),
	}

	for name, tc := range map[string]struct {
		absentHosts string
		absentRanks string
		results     system.MemberResults
		expErr      error
	}{
		"no errors": {
			results: successResults,
		},
		"absent hosts": {
			absentHosts: "foo-[1-23]",
			results:     successResults,
			expErr:      errors.New("non-existent hosts foo-[1-23]"),
		},
		"absent ranks": {
			absentRanks: "1-23",
			results:     successResults,
			expErr:      errors.New("non-existent ranks 1-23"),
		},
		"failed ranks": {
			results: failedResults,
			expErr:  errors.New("check results for failed ranks 0,2"),
		},
		"absent hosts and ranks with failed ranks": {
			absentHosts: "foo-[1-23]",
			absentRanks: "1-23",
			results:     failedResults,
			expErr: errors.New("non-existent hosts foo-[1-23], " +
				"non-existent ranks 1-23, check results for failed ranks 0,2"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			resp := new(SystemStopResp)
			ahs := hostlist.MustCreateSet(tc.absentHosts)
			resp.AbsentHosts.ReplaceSet(ahs)
			ars := system.MustCreateRankSet(tc.absentRanks)
			resp.AbsentRanks.ReplaceSet(ars)
			resp.Results = tc.results

			common.CmpErr(t, tc.expErr, resp.Errors())
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
				&mgmtpb.SystemResetFormatResp{
					Results: []*sharedpb.RankResult{},
				},
			),
			// indicates SystemReformat internally proceeded to invoke StorageFormat RPCs
			// which is expected when no host errors are returned
			expErr: errors.New("unpack"),
		},
		"single host dual rank": {
			req: new(SystemResetFormatReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&mgmtpb.SystemResetFormatResp{
					Results: []*sharedpb.RankResult{
						{
							Rank: 1, Action: "reset format",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  "10.0.0.1:10001",
						},
						{
							Rank: 2, Action: "reset format",
							State: system.MemberStateAwaitFormat.String(),
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
				&mgmtpb.SystemResetFormatResp{
					Results: []*sharedpb.RankResult{
						{
							Rank: 1, Action: "reset format",
							State:   system.MemberStateStopped.String(),
							Addr:    "10.0.0.1:10001",
							Errored: true, Msg: "didn't start",
						},
						{
							Rank: 2, Action: "reset format",
							State: system.MemberStateAwaitFormat.String(),
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
				&mgmtpb.SystemResetFormatResp{
					Results: []*sharedpb.RankResult{
						{
							Rank: 1, Action: "reset format",
							State:   system.MemberStateStopped.String(),
							Addr:    "10.0.0.1:10001",
							Errored: true, Msg: "didn't start",
						},
						{
							Rank: 2, Action: "reset format",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  "10.0.0.1:10001",
						},
						{
							Rank: 3, Action: "reset format",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  "10.0.0.2:10001",
						},
						{
							Rank: 4, Action: "reset format",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  "10.0.0.2:10001",
						},
						{
							Rank: 5, Action: "reset format",
							State:   system.MemberStateStopped.String(),
							Addr:    "10.0.0.3:10001",
							Errored: true, Msg: "didn't start",
						},
						{
							Rank: 6, Action: "reset format",
							State:   system.MemberStateErrored.String(),
							Addr:    "10.0.0.3:10001",
							Errored: true, Msg: "something bad",
						},
						{
							Rank: 7, Action: "reset format",
							State:   system.MemberStateStopped.String(),
							Addr:    "10.0.0.4:10001",
							Errored: true, Msg: "didn't start",
						},
						{
							Rank: 8, Action: "reset format",
							State:   system.MemberStateErrored.String(),
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

func TestControl_SystemNotify(t *testing.T) {
	rasEventRankDown := events.NewRankDownEvent("foo", 0, 0, common.NormalExit)

	for name, tc := range map[string]struct {
		req     *SystemNotifyReq
		uErr    error
		uResp   *UnaryResponse
		expResp *SystemNotifyResp
		expErr  error
	}{
		"nil req": {
			req:    nil,
			expErr: errors.New("nil request"),
		},
		"nil event": {
			req:    &SystemNotifyReq{},
			expErr: errors.New("nil event in request"),
		},
		"zero sequence number": {
			req:    &SystemNotifyReq{Event: rasEventRankDown},
			expErr: errors.New("invalid sequence"),
		},
		"local failure": {
			req: &SystemNotifyReq{
				Event:    rasEventRankDown,
				Sequence: 1,
			},
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &SystemNotifyReq{
				Event:    rasEventRankDown,
				Sequence: 1,
			},
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"empty response": {
			req: &SystemNotifyReq{
				Event:    rasEventRankDown,
				Sequence: 1,
			},
			uResp:   MockMSResponse("10.0.0.1:10001", nil, &sharedpb.ClusterEventResp{}),
			expResp: &SystemNotifyResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			rpcClient := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemNotify(context.TODO(), rpcClient, tc.req)
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

func TestControl_EventForwarder_OnEvent(t *testing.T) {
	rasEventRankDownFwdable := events.NewRankDownEvent("foo", 0, 0, common.NormalExit)
	rasEventRankDown := events.NewRankDownEvent("foo", 0, 0, common.NormalExit).
		WithForwardable(false)

	for name, tc := range map[string]struct {
		aps            []string
		event          *events.RASEvent
		nilClient      bool
		expInvokeCount int
	}{
		"nil event": {
			event: nil,
		},
		"missing access points": {
			event: rasEventRankDownFwdable,
		},
		"successful forward": {
			event:          rasEventRankDownFwdable,
			aps:            []string{"192.168.1.1"},
			expInvokeCount: 2,
		},
		"skip non-forwardable event": {
			event: rasEventRankDown,
			aps:   []string{"192.168.1.1"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			expNextSeq := uint64(tc.expInvokeCount + 1)

			mi := NewMockInvoker(log, &MockInvokerConfig{})
			if tc.nilClient {
				mi = nil
			}

			callCount := tc.expInvokeCount
			if callCount == 0 {
				callCount++ // call at least once
			}

			ef := NewEventForwarder(mi, tc.aps)
			for i := 0; i < callCount; i++ {
				ef.OnEvent(context.TODO(), tc.event)
			}

			common.AssertEqual(t, tc.expInvokeCount, mi.invokeCount,
				"unexpected number of rpc calls")
			common.AssertEqual(t, expNextSeq, <-ef.seq,
				"unexpected next forwarding sequence")
		})
	}
}

// In real syslog implementation we would see entries logged to logger specific
// to a given priority, here we just check the correct prefix (maps to severity)
// is printed which verifies the event was written to the correct logger.
func TestControl_EventLogger_OnEvent(t *testing.T) {
	var mockSyslogBuf *strings.Builder
	mockNewSyslogger := func(prio syslog.Priority, _ int) (*log.Logger, error) {
		return log.New(mockSyslogBuf, fmt.Sprintf("prio%d ", prio), log.LstdFlags), nil
	}
	mockNewSysloggerFail := func(prio syslog.Priority, _ int) (*log.Logger, error) {
		return nil, errors.Errorf("failed to create new syslogger (prio %d)", prio)
	}

	rasEventRankDown := events.NewRankDownEvent("foo", 0, 0, common.NormalExit)
	rasEventRankDownFwded := events.NewRankDownEvent("foo", 0, 0, common.NormalExit).
		WithForwarded(true)

	for name, tc := range map[string]struct {
		event           *events.RASEvent
		newSyslogger    newSysloggerFn
		expShouldLog    bool
		expShouldLogSys bool
	}{
		"nil event": {
			event: nil,
		},
		"forwarded event is not logged": {
			event: rasEventRankDownFwded,
		},
		"not forwarded error event gets logged": {
			event:           rasEventRankDown,
			expShouldLog:    true,
			expShouldLogSys: true,
		},
		"not forwarded info event gets logged": {
			event: events.NewGenericEvent(events.RASID(math.MaxInt32-1),
				events.RASSeverityInfo, "DAOS generic test event",
				`{"people":["bill","steve","bob"]}`),
			expShouldLog:    true,
			expShouldLogSys: true,
		},
		"sysloggers not created": {
			event:           rasEventRankDown,
			newSyslogger:    mockNewSysloggerFail,
			expShouldLog:    true,
			expShouldLogSys: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			logBasic, bufBasic := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, bufBasic)

			mockSyslogBuf = &strings.Builder{}

			if tc.newSyslogger == nil {
				tc.newSyslogger = mockNewSyslogger
			}

			el := newEventLogger(logBasic, tc.newSyslogger)
			el.OnEvent(context.TODO(), tc.event)

			// check event logged to control plane
			common.AssertEqual(t, tc.expShouldLog,
				strings.Contains(bufBasic.String(), "RAS "),
				"unexpected log output")

			slStr := mockSyslogBuf.String()
			t.Logf("syslog out: %s", slStr)
			if !tc.expShouldLogSys {
				common.AssertTrue(t, slStr == "",
					"expected syslog to be empty")
				return
			}
			prioStr := fmt.Sprintf("prio%d ", tc.event.Severity.SyslogPriority())
			sevOut := "sev: [" + tc.event.Severity.String() + "]"

			// check event logged to correct mock syslogger
			common.AssertEqual(t, 1, strings.Count(slStr, "RAS EVENT"),
				"unexpected number of events in syslog")
			common.AssertTrue(t, strings.Contains(slStr, sevOut),
				"syslog output missing severity")
			common.AssertTrue(t, strings.HasPrefix(slStr, prioStr),
				"syslog output missing syslog priority")
		})
	}
}
