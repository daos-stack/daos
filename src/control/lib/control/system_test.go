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
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host1", "remote failed"}),
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host3", "connection refused"}),
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

			gotResp, gotErr := StartRanks(context.TODO(), mi, &RanksReq{Ranks: []system.Rank{0, 1, 2, 3}})
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host1", "remote failed"}),
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host3", "connection refused"}),
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

			gotResp, gotErr := PrepShutdownRanks(context.TODO(), mi, &RanksReq{Ranks: []system.Rank{0, 1, 2, 3}})
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host1", "remote failed"}),
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host3", "connection refused"}),
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

			gotResp, gotErr := StopRanks(context.TODO(), mi, &RanksReq{Ranks: []system.Rank{0, 1, 2, 3}, Force: true})
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host1", "remote failed"}),
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host3", "connection refused"}),
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

			gotResp, gotErr := PingRanks(context.TODO(), mi, &RanksReq{Ranks: []system.Rank{0, 1, 2, 3}})
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

func TestControl_SystemReformat(t *testing.T) {
	for name, tc := range map[string]struct {
		uErr    error
		uResp   *UnaryResponse
		expResp *StorageFormatResp
		expErr  error
	}{
		"local failure": {
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"no results": {
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
					HostErrors: mockHostErrorsMap(t, &mockHostError{
						"10.0.0.1:10001", "1 rank failed: didn't start",
					}),
				},
			},
		},
		"multiple hosts dual rank mixed results": {
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
						&mockHostError{"10.0.0.4:10001", "2 ranks failed: didn't start"},
						&mockHostError{"10.0.0.[1,3]:10001", "1 rank failed: didn't start"},
						&mockHostError{"10.0.0.3:10001", "1 rank failed: something bad"},
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

			gotResp, gotErr := SystemReformat(context.TODO(), mi, &SystemResetFormatReq{})
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
