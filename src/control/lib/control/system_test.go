//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

func TestControl_IsMSConnectionFailure(t *testing.T) {
	for name, tc := range map[string]struct {
		err       error
		expResult bool
	}{
		"nil": {},
		"MS connection error": {
			err:       errMSConnectionFailure,
			expResult: true,
		},
		"wrapped MS connection error": {
			err:       errors.Wrap(errMSConnectionFailure, "something bad happened"),
			expResult: true,
		},
		"other error": {
			err: errors.New("something went wrong"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, IsMSConnectionFailure(tc.err), "")
		})
	}
}

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
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: &UnaryResponse{Responses: tc.uResps},
			})

			gotResp, gotErr := StartRanks(test.Context(t), mi, &RanksReq{Ranks: "0-3"})
			test.CmpErr(t, tc.expErr, gotErr)
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
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: &UnaryResponse{Responses: tc.uResps},
			})

			gotResp, gotErr := PrepShutdownRanks(test.Context(t), mi, &RanksReq{Ranks: "0-3"})
			test.CmpErr(t, tc.expErr, gotErr)
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
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: &UnaryResponse{Responses: tc.uResps},
			})

			gotResp, gotErr := StopRanks(test.Context(t), mi, &RanksReq{Ranks: "0-3", Force: true})
			test.CmpErr(t, tc.expErr, gotErr)
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
				{Addr: "10.0.0.1:10001", Rank: ranklist.Rank(0)},
			},
			expRankErrs: make(map[string][]string),
			expHosts:    []string{"10.0.0.1:10001"},
		},
		"successful result missing address": {
			results: system.MemberResults{
				{Addr: "", Rank: ranklist.Rank(0)},
			},
			expErrMsg: "host address missing for rank 0 result",
		},
		"failed result": {
			results: system.MemberResults{
				{
					Addr: "10.0.0.1:10001", Rank: ranklist.Rank(0),
					Errored: true, Msg: "didn't start",
				},
			},
			expRankErrs: map[string][]string{"didn't start": {"10.0.0.1:10001"}},
			expHosts:    []string{},
		},
		"failed result missing error message": {
			results: system.MemberResults{
				{
					Addr: "10.0.0.1:10001", Rank: ranklist.Rank(0),
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
				{Addr: "10.0.0.1:10001", Rank: ranklist.Rank(0)},
				{Addr: "10.0.0.1:10001", Rank: ranklist.Rank(1)},
				{
					Addr: "10.0.0.2:10001", Rank: ranklist.Rank(2),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.2:10001", Rank: ranklist.Rank(3),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.3:10001", Rank: ranklist.Rank(4),
					Errored: true, Msg: "something bad",
				},
				{
					Addr: "10.0.0.3:10001", Rank: ranklist.Rank(5),
					Errored: true, Msg: "didn't start",
				},
				{
					Addr: "10.0.0.4:10001", Rank: ranklist.Rank(6),
					Errored: true, Msg: "something bad",
				},
				{Addr: "10.0.0.4:10001", Rank: ranklist.Rank(7)},
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
			test.ExpectError(t, err, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			test.AssertEqual(t, tc.expRankErrs, rankErrs, name)
			test.AssertStringsEqual(t, tc.expHosts, hosts, "host list")
		})
	}
}

func TestControl_SystemQueryReq_getStateMask(t *testing.T) {
	for name, tc := range map[string]struct {
		req     *SystemQueryReq
		expMask system.MemberState
		expErr  error
	}{
		"not-ok": {
			req: &SystemQueryReq{
				NotOK: true,
			},
			expMask: system.AllMemberFilter &^ system.MemberStateJoined,
		},
		"with-states": {
			req: &SystemQueryReq{
				WantedStates: system.MemberStateJoined | system.MemberStateExcluded,
			},
			expMask: system.MemberStateJoined | system.MemberStateExcluded,
		},
		"with-states; bad state": {
			req: &SystemQueryReq{
				WantedStates: -1,
			},
			expErr: errors.New("invalid member states bitmask -1"),
		},
		"vanilla": {
			req:     &SystemQueryReq{},
			expMask: system.AllMemberFilter,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotMask, gotErr := tc.req.getStateMask()
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expMask, gotMask, name)
		})
	}
}

func TestControl_SystemQuery(t *testing.T) {
	testHS := hostlist.MustCreateSet("foo-[1-23]")
	testReqHS := new(SystemQueryReq)
	testReqHS.Hosts.Replace(testHS)
	testRespHS := new(SystemQueryResp)
	testRespHS.AbsentHosts.Replace(testHS)

	testRS := ranklist.MustCreateRankSet("1-23")
	testReqRS := new(SystemQueryReq)
	testReqRS.Ranks.Replace(testRS)
	testRespRS := new(SystemQueryResp)
	testRespRS.AbsentRanks.Replace(testRS)

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
							Uuid:        test.MockUUID(1),
							State:       system.MemberStateReady.String(),
							Addr:        "10.0.0.1:10001",
							FaultDomain: fdStrs[1],
						},
						{
							Rank:        2,
							Uuid:        test.MockUUID(2),
							State:       system.MemberStateReady.String(),
							Addr:        "10.0.0.1:10001",
							FaultDomain: fdStrs[2],
						},
						{
							Rank:        0,
							Uuid:        test.MockUUID(0),
							State:       system.MemberStateStopped.String(),
							Addr:        "10.0.0.2:10001",
							FaultDomain: fdStrs[0],
						},
						{
							Rank:        3,
							Uuid:        test.MockUUID(3),
							State:       system.MemberStateStopped.String(),
							Addr:        "10.0.0.2:10001",
							FaultDomain: fdStrs[3],
						},
					},
				},
			),
			expResp: &SystemQueryResp{
				Members: system.Members{
					system.MockMemberFullSpec(t, 1, test.MockUUID(1), "",
						test.MockHostAddr(1), system.MemberStateReady).
						WithFaultDomain(fds[1]),
					system.MockMemberFullSpec(t, 2, test.MockUUID(2), "",
						test.MockHostAddr(1), system.MemberStateReady).
						WithFaultDomain(fds[2]),
					system.MockMemberFullSpec(t, 0, test.MockUUID(0), "",
						test.MockHostAddr(2), system.MemberStateStopped).
						WithFaultDomain(fds[0]),
					system.MockMemberFullSpec(t, 3, test.MockUUID(3), "",
						test.MockHostAddr(2), system.MemberStateStopped).
						WithFaultDomain(fds[3]),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemQuery(test.Context(t), mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(SystemQueryResp{}, system.Member{}),
				cmpopts.IgnoreFields(system.Member{}, "LastUpdate"),
			}
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
			resp.AbsentHosts.Replace(ahs)
			ars := ranklist.MustCreateRankSet(tc.absentRanks)
			resp.AbsentRanks.Replace(ars)

			test.CmpErr(t, tc.expErr, resp.Errors())
		})
	}
}

func TestControl_SystemStart(t *testing.T) {
	testHS := hostlist.MustCreateSet("foo-[1-23]")
	testReqHS := new(SystemStartReq)
	testReqHS.Hosts.Replace(testHS)
	testRespHS := new(SystemStartResp)
	testRespHS.AbsentHosts.Replace(testHS)

	testRS := ranklist.MustCreateRankSet("1-23")
	testReqRS := new(SystemStartReq)
	testReqRS.Ranks.Replace(testRS)
	testRespRS := new(SystemStartResp)
	testRespRS.AbsentRanks.Replace(testRS)

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
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemStart(test.Context(t), mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
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
			resp.AbsentHosts.Replace(ahs)
			ars := ranklist.MustCreateRankSet(tc.absentRanks)
			resp.AbsentRanks.Replace(ars)
			resp.Results = tc.results

			test.CmpErr(t, tc.expErr, resp.Errors())
		})
	}
}

func TestControl_SystemStop(t *testing.T) {
	testHS := hostlist.MustCreateSet("foo-[1-23]")
	testReqHS := new(SystemStopReq)
	testReqHS.Hosts.Replace(testHS)
	testRespHS := new(SystemStopResp)
	testRespHS.AbsentHosts.Replace(testHS)

	testRS := ranklist.MustCreateRankSet("1-23")
	testReqRS := new(SystemStopReq)
	testReqRS.Ranks.Replace(testRS)
	testRespRS := new(SystemStopResp)
	testRespRS.AbsentRanks.Replace(testRS)

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
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemStop(test.Context(t), mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
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
			resp.AbsentHosts.Replace(ahs)
			ars := ranklist.MustCreateRankSet(tc.absentRanks)
			resp.AbsentRanks.Replace(ars)
			resp.Results = tc.results

			test.CmpErr(t, tc.expErr, resp.Errors())
		})
	}
}

func TestControl_SystemExclude(t *testing.T) {
	for name, tc := range map[string]struct {
		req     *SystemExcludeReq
		uErr    error
		uResp   *UnaryResponse
		expResp *SystemExcludeResp
		expErr  error
	}{
		"nil req": {
			req:    nil,
			expErr: errors.New("nil *control.SystemExcludeReq request"),
		},
		"local failure": {
			req:    new(SystemExcludeReq),
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req:    new(SystemExcludeReq),
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"dual host dual rank": {
			req: new(SystemExcludeReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&mgmtpb.SystemExcludeResp{
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
			expResp: &SystemExcludeResp{
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
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemExclude(test.Context(t), mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{cmpopts.IgnoreUnexported(SystemExcludeResp{}, system.MemberResult{})}
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDmg_System_checkSystemErase(t *testing.T) {
	for name, tc := range map[string]struct {
		uErr, expErr error
		members      []*mgmtpb.SystemMember
	}{
		"failed system query": {
			uErr:   errors.New("system failed"),
			expErr: errors.New("system failed"),
		},
		"not replica": {
			uErr: &system.ErrNotReplica{},
		},
		"raft unavailable": {
			uErr: system.ErrRaftUnavail,
		},
		"empty membership": {},
		"rank not stopped": {
			members: []*mgmtpb.SystemMember{
				{Rank: 0, State: system.MemberStateStopped.String()},
				{Rank: 1, State: system.MemberStateJoined.String()},
			},
			expErr: errors.New("system erase requires the following 1 rank to be stopped: 1"),
		},
		"ranks not stopped": {
			members: []*mgmtpb.SystemMember{
				{Rank: 0, State: system.MemberStateJoined.String()},
				{Rank: 1, State: system.MemberStateStopped.String()},
				{Rank: 5, State: system.MemberStateJoined.String()},
				{Rank: 2, State: system.MemberStateJoined.String()},
				{Rank: 4, State: system.MemberStateJoined.String()},
				{Rank: 3, State: system.MemberStateJoined.String()},
				{Rank: 6, State: system.MemberStateStopped.String()},
			},
			expErr: errors.New("system erase requires the following 5 ranks to be stopped: 0,2-5"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError: tc.uErr,
				UnaryResponse: MockMSResponse("host1", nil,
					&mgmtpb.SystemQueryResp{Members: tc.members}),
			})

			err := checkSystemErase(test.Context(t), mi)
			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestControl_SystemErase(t *testing.T) {
	member1 := system.MockMember(t, 1, system.MemberStateAwaitFormat)
	member3 := system.MockMember(t, 3, system.MemberStateAwaitFormat)
	member3.Addr = member1.Addr
	member2 := system.MockMember(t, 2, system.MemberStateAwaitFormat)
	member4 := system.MockMember(t, 4, system.MemberStateAwaitFormat)
	member4.Addr = member2.Addr
	member5 := system.MockMember(t, 5, system.MemberStateAwaitFormat)
	member6 := system.MockMember(t, 6, system.MemberStateAwaitFormat)
	member6.Addr = member5.Addr
	member7 := system.MockMember(t, 7, system.MemberStateAwaitFormat)
	member8 := system.MockMember(t, 8, system.MemberStateAwaitFormat)
	member8.Addr = member7.Addr
	mockMemberResult := func(m *system.Member, action string, err error, state system.MemberState) *system.MemberResult {
		mr := system.NewMemberResult(m.Rank, err, state, action)
		mr.Addr = m.Addr.String()
		return mr
	}

	for name, tc := range map[string]struct {
		req     *SystemEraseReq
		uErr    error
		uResp   *UnaryResponse
		expResp *SystemEraseResp
		expErr  error
	}{
		"nil req": {
			req:    nil,
			expErr: errors.New("nil *control.SystemEraseReq request"),
		},
		"local failure": {
			req:    new(SystemEraseReq),
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req:    new(SystemEraseReq),
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"remote unavailable": {
			req:    new(SystemEraseReq),
			uResp:  MockMSResponse("host1", system.ErrRaftUnavail, nil),
			expErr: system.ErrRaftUnavail,
		},
		"single host dual rank": {
			req: new(SystemEraseReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&mgmtpb.SystemEraseResp{
					Results: []*sharedpb.RankResult{
						{
							Rank: member1.Rank.Uint32(), Action: "system erase",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  member1.Addr.String(),
						},
						{
							Rank: member2.Rank.Uint32(), Action: "system erase",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  member2.Addr.String(),
						},
					},
				},
			),
			expResp: &SystemEraseResp{
				Results: system.MemberResults{
					mockMemberResult(member1, "system erase", nil, system.MemberStateAwaitFormat),
					mockMemberResult(member2, "system erase", nil, system.MemberStateAwaitFormat),
				},
			},
		},
		"single host dual rank one failed": {
			req: new(SystemEraseReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&mgmtpb.SystemEraseResp{
					Results: []*sharedpb.RankResult{
						{
							Rank: member1.Rank.Uint32(), Action: "system erase",
							State:   system.MemberStateErrored.String(),
							Addr:    member1.Addr.String(),
							Errored: true, Msg: "erase failed",
						},
						{
							Rank: member2.Rank.Uint32(), Action: "system erase",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  member2.Addr.String(),
						},
					},
				},
			),
			expResp: &SystemEraseResp{
				HostErrorsResp: MockHostErrorsResp(t,
					&MockHostError{
						Hosts: member1.Addr.String(),
						Error: "1 rank failed: erase failed",
					},
				),
				Results: system.MemberResults{
					mockMemberResult(member1, "system erase", errors.New("erase failed"), system.MemberStateStopped),
					mockMemberResult(member2, "system erase", nil, system.MemberStateAwaitFormat),
				},
			},
		},
		"multiple hosts dual rank mixed results": {
			req: new(SystemEraseReq),
			uResp: MockMSResponse("10.0.0.1:10001", nil,
				&mgmtpb.SystemEraseResp{
					Results: []*sharedpb.RankResult{
						{
							Rank: member1.Rank.Uint32(), Action: "system erase",
							State:   system.MemberStateErrored.String(),
							Addr:    member1.Addr.String(),
							Errored: true, Msg: "erase failed",
						},
						{
							Rank: member2.Rank.Uint32(), Action: "system erase",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  member2.Addr.String(),
						},
						{
							Rank: member3.Rank.Uint32(), Action: "system erase",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  member3.Addr.String(),
						},
						{
							Rank: member4.Rank.Uint32(), Action: "system erase",
							State: system.MemberStateAwaitFormat.String(),
							Addr:  member4.Addr.String(),
						},
						{
							Rank: member5.Rank.Uint32(), Action: "system erase",
							State:   system.MemberStateErrored.String(),
							Addr:    member5.Addr.String(),
							Errored: true, Msg: "erase failed",
						},
						{
							Rank: member6.Rank.Uint32(), Action: "system erase",
							State:   system.MemberStateErrored.String(),
							Addr:    member6.Addr.String(),
							Errored: true, Msg: "something bad",
						},
						{
							Rank: member7.Rank.Uint32(), Action: "system erase",
							State:   system.MemberStateErrored.String(),
							Addr:    member7.Addr.String(),
							Errored: true, Msg: "erase failed",
						},
						{
							Rank: member8.Rank.Uint32(), Action: "system erase",
							State:   system.MemberStateErrored.String(),
							Addr:    member8.Addr.String(),
							Errored: true, Msg: "erase failed",
						},
					},
				},
			),
			expResp: &SystemEraseResp{
				HostErrorsResp: MockHostErrorsResp(t,
					&MockHostError{
						Hosts: hostlist.MustCreateSet(
							member1.Addr.String() + "," + member5.Addr.String(),
						).String(),
						Error: "1 rank failed: erase failed",
					},
					&MockHostError{
						Hosts: member7.Addr.String(),
						Error: "2 ranks failed: erase failed",
					},
					&MockHostError{
						Hosts: member5.Addr.String(),
						Error: "1 rank failed: something bad",
					},
				),
				Results: system.MemberResults{
					mockMemberResult(member1, "system erase", errors.New("erase failed"), system.MemberStateStopped),
					mockMemberResult(member2, "system erase", nil, system.MemberStateAwaitFormat),
					mockMemberResult(member3, "system erase", nil, system.MemberStateAwaitFormat),
					mockMemberResult(member4, "system erase", nil, system.MemberStateAwaitFormat),
					mockMemberResult(member5, "system erase", errors.New("erase failed"), system.MemberStateStopped),
					mockMemberResult(member6, "system erase", errors.New("something bad"), system.MemberStateErrored),
					mockMemberResult(member7, "system erase", errors.New("erase failed"), system.MemberStateStopped),
					mockMemberResult(member8, "system erase", errors.New("erase failed"), system.MemberStateErrored),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemErase(test.Context(t), mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_SystemJoin_RetryableErrors(t *testing.T) {
	for name, testErr := range map[string]error{
		"system not formatted": system.ErrUninitialized,
		"system unavailable":   system.ErrRaftUnavail,
		"connection closed":    FaultConnectionClosed(""),
		"connection refused":   FaultConnectionRefused(""),
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			client := NewMockInvoker(log, &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", testErr, nil),
					MockMSResponse("", nil, &mgmtpb.JoinResp{Rank: 42}),
				},
			})

			gotResp, gotErr := SystemJoin(test.Context(t), client, &SystemJoinReq{})
			if gotErr != nil {
				t.Fatalf("unexpected error: %v", gotErr)
			}

			expResp := &SystemJoinResp{Rank: 42}
			if diff := cmp.Diff(expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_SystemJoin_Timeouts(t *testing.T) {
	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		expResp *SystemJoinResp
		expErr  error
	}{
		"outer context is canceled; request times out": {
			mic: &MockInvokerConfig{
				ReqTimeout: 1 * time.Nanosecond,
				UnaryResponseDelays: [][]time.Duration{
					{time.Millisecond},
				},
			},
			expErr: FaultRpcTimeout(&SystemJoinReq{unaryRequest: unaryRequest{request: request{timeout: 1 * time.Nanosecond}}}),
		},
		"inner context is canceled; request is retried": {
			mic: &MockInvokerConfig{
				ReqTimeout:   500 * time.Millisecond, // outer timeout
				RetryTimeout: 10 * time.Millisecond,  // inner timeout
				UnaryResponseSet: []*UnaryResponse{
					{
						fromMS: true,
						Responses: []*HostResponse{
							{
								Error: FaultConnectionClosed(""),
							},
							{
								Error: FaultConnectionRefused(""),
							},
							{
								// should be delayed to trigger inner timeout
								Error: errors.New("the timeout should be retried"),
							},
						},
					},
					// on retry, the request should succeed
					MockMSResponse("", nil, &mgmtpb.JoinResp{Rank: 42}),
				},
				UnaryResponseDelays: [][]time.Duration{
					{
						2 * time.Millisecond,
						5 * time.Millisecond,
						20 * time.Millisecond,
					},
				},
			},
			expResp: &SystemJoinResp{Rank: 42},
		},
		"MS response contains timeout; request is retried": {
			mic: &MockInvokerConfig{
				ReqTimeout:   100 * time.Millisecond, // outer timeout
				RetryTimeout: 10 * time.Millisecond,  // inner timeout
				UnaryResponseSet: []*UnaryResponse{
					{
						fromMS: true,
						Responses: []*HostResponse{
							{
								Error: FaultConnectionClosed(""),
							},
							{
								Error: FaultConnectionRefused(""),
							},
							{
								Error: context.DeadlineExceeded,
							},
						},
					},
					// on retry, the request should succeed
					MockMSResponse("", nil, &mgmtpb.JoinResp{Rank: 42}),
				},
			},
			expResp: &SystemJoinResp{Rank: 42},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.Context(t)
			client := NewMockInvoker(log, tc.mic)
			gotResp, gotErr := SystemJoin(ctx, client, &SystemJoinReq{})
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_SystemSetAttr(t *testing.T) {
	for name, tc := range map[string]struct {
		req    *SystemSetAttrReq
		mic    *MockInvokerConfig
		expErr error
	}{
		"nil req": {
			expErr: errors.New("nil"),
		},
		"req fails": {
			req: &SystemSetAttrReq{
				Attributes: map[string]string{
					"foo": "bar",
				},
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", errors.New("error"), nil),
				},
			},
			expErr: errors.New("error"),
		},
		"empty attributes": {
			req: &SystemSetAttrReq{
				Attributes: map[string]string{},
			},
			expErr: errors.New("cannot be empty"),
		},
		"success": {
			req: &SystemSetAttrReq{
				Attributes: map[string]string{
					"foo": "bar",
					"baz": "qux",
				},
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", nil, &mgmtpb.DaosResp{}),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			client := NewMockInvoker(log, tc.mic)
			gotErr := SystemSetAttr(test.Context(t), client, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestControl_SystemGetAttr(t *testing.T) {
	for name, tc := range map[string]struct {
		req     *SystemGetAttrReq
		mic     *MockInvokerConfig
		expResp *SystemGetAttrResp
		expErr  error
	}{
		"nil req": {
			expErr: errors.New("nil"),
		},
		"req fails": {
			req: &SystemGetAttrReq{
				Keys: []string{"foo", "baz"},
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", errors.New("error"), nil),
				},
			},
			expErr: errors.New("error"),
		},
		"success": {
			req: &SystemGetAttrReq{
				Keys: []string{"foo", "baz"},
			},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", nil, &mgmtpb.SystemGetAttrResp{
						Attributes: map[string]string{
							"foo": "bar",
							"baz": "qux",
						},
					}),
				},
			},
			expResp: &SystemGetAttrResp{
				Attributes: map[string]string{
					"foo": "bar",
					"baz": "qux",
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			client := NewMockInvoker(log, tc.mic)
			gotResp, gotErr := SystemGetAttr(test.Context(t), client, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
