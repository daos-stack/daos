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

package server

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_CtlSvc_rpcFanout(t *testing.T) {
	for name, tc := range map[string]struct {
		members        system.Members
		fanReq         fanoutRequest
		mResps         []*control.HostResponse
		hostErrors     control.HostErrorsMap
		expResults     system.MemberResults
		expRanks       string
		expMembers     system.Members
		expAbsentRanks string
		expAbsentHosts string
		expErrMsg      string
	}{
		"nil method in request": {
			expErrMsg: "fanout request with nil method",
		},
		"hosts and ranks both specified": {
			fanReq: fanoutRequest{
				Method: control.PingRanks, Hosts: "foo-[0-99]", Ranks: "0-99",
			},
			expErrMsg: "ranklist and hostlist cannot both be set in request",
		},
		"empty membership": {
			fanReq:     fanoutRequest{Method: control.PingRanks},
			expMembers: system.Members{},
		},
		"bad hosts in request": {
			fanReq:    fanoutRequest{Method: control.PingRanks, Hosts: "123"},
			expErrMsg: "invalid hostname \"123\"",
		},
		"bad ranks in request": {
			fanReq:    fanoutRequest{Method: control.PingRanks, Ranks: "foo"},
			expErrMsg: "unexpected alphabetic character(s)",
		},
		"unfiltered ranks": {
			fanReq: fanoutRequest{Method: control.PingRanks},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateJoined),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateJoined),
				system.NewMember(system.Rank(6), common.MockUUID(6), "", common.MockHostAddr(4), system.MemberStateJoined),
				system.NewMember(system.Rank(7), common.MockUUID(7), "", common.MockHostAddr(4), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "fatality",
								State: uint32(system.MemberStateErrored),
							},
							{Rank: 3, State: uint32(system.MemberStateJoined)},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank:  1,
								State: uint32(system.MemberStateJoined),
							},
							{
								Rank:  2,
								State: uint32(system.MemberStateJoined),
							},
						},
					},
				},
				{
					Addr:  common.MockHostAddr(3).String(),
					Error: errors.New("connection refused"),
				},
				{
					Addr:  common.MockHostAddr(4).String(),
					Error: errors.New("connection refused"),
				},
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			expResults: system.MemberResults{
				{
					Rank: 0, Errored: true, Msg: "fatality",
					Addr:  common.MockHostAddr(1).String(),
					State: system.MemberStateErrored,
				},
				{
					Rank: 3, Addr: common.MockHostAddr(1).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 1, Addr: common.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 2, Addr: common.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 4, Msg: "connection refused",
					Addr:  common.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 5, Msg: "connection refused",
					Addr:  common.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 6, Msg: "connection refused",
					Addr:  common.MockHostAddr(4).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 7, Msg: "connection refused",
					Addr:  common.MockHostAddr(4).String(),
					State: system.MemberStateUnresponsive,
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateErrored).
					WithInfo("fatality"),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateUnresponsive).
					WithInfo("connection refused"),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateUnresponsive).
					WithInfo("connection refused"),
				system.NewMember(system.Rank(6), common.MockUUID(6), "", common.MockHostAddr(4), system.MemberStateUnresponsive).
					WithInfo("connection refused"),
				system.NewMember(system.Rank(7), common.MockUUID(7), "", common.MockHostAddr(4), system.MemberStateUnresponsive).
					WithInfo("connection refused"),
			},
			expRanks: "0-7",
		},
		"filtered and oversubscribed ranks": {
			fanReq: fanoutRequest{Method: control.PingRanks, Ranks: "0-3,6-10"},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateJoined),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateJoined),
				system.NewMember(system.Rank(6), common.MockUUID(6), "", common.MockHostAddr(4), system.MemberStateJoined),
				system.NewMember(system.Rank(7), common.MockUUID(7), "", common.MockHostAddr(4), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "fatality",
								State: uint32(system.MemberStateErrored),
							},
							{
								Rank:  3,
								State: uint32(system.MemberStateJoined),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 1, State: uint32(system.MemberStateJoined)},
							{Rank: 2, State: uint32(system.MemberStateJoined)},
						},
					},
				},
				{
					Addr:  common.MockHostAddr(4).String(),
					Error: errors.New("connection refused"),
				},
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			// results from ranks outside rank list absent
			expResults: system.MemberResults{
				{
					Rank: 0, Errored: true, Msg: "fatality",
					Addr:  common.MockHostAddr(1).String(),
					State: system.MemberStateErrored,
				},
				{
					Rank: 3, Addr: common.MockHostAddr(1).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 1, Addr: common.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 2, Addr: common.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 6, Msg: "connection refused",
					Addr:  common.MockHostAddr(4).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 7, Msg: "connection refused",
					Addr:  common.MockHostAddr(4).String(),
					State: system.MemberStateUnresponsive,
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateErrored).
					WithInfo("fatality"),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateJoined),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateJoined),
				system.NewMember(system.Rank(6), common.MockUUID(6), "", common.MockHostAddr(4), system.MemberStateUnresponsive).
					WithInfo("connection refused"),
				system.NewMember(system.Rank(7), common.MockUUID(7), "", common.MockHostAddr(4), system.MemberStateUnresponsive).
					WithInfo("connection refused"),
			},
			expRanks:       "0-3,6-7",
			expAbsentRanks: "8-10",
		},
		"filtered and oversubscribed hosts": {
			fanReq: fanoutRequest{Method: control.PingRanks, Hosts: "10.0.0.[1-3,5]"},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateJoined),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateJoined),
				system.NewMember(system.Rank(6), common.MockUUID(6), "", common.MockHostAddr(4), system.MemberStateJoined),
				system.NewMember(system.Rank(7), common.MockUUID(7), "", common.MockHostAddr(4), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "fatality",
								State: uint32(system.MemberStateErrored),
							},
							{
								Rank:  3,
								State: uint32(system.MemberStateJoined),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 1, State: uint32(system.MemberStateJoined)},
							{Rank: 2, State: uint32(system.MemberStateJoined)},
						},
					},
				},
				{
					Addr:  common.MockHostAddr(3).String(),
					Error: errors.New("connection refused"),
				},
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			// results from ranks outside rank list absent
			expResults: system.MemberResults{
				{
					Rank: 0, Errored: true, Msg: "fatality",
					Addr:  common.MockHostAddr(1).String(),
					State: system.MemberStateErrored,
				},
				{
					Rank: 3, Addr: common.MockHostAddr(1).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 1, Addr: common.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 2, Addr: common.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 4, Msg: "connection refused",
					Addr:  common.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 5, Msg: "connection refused",
					Addr:  common.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateErrored).
					WithInfo("fatality"),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateUnresponsive).
					WithInfo("connection refused"),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateUnresponsive).
					WithInfo("connection refused"),
				system.NewMember(system.Rank(6), common.MockUUID(6), "", common.MockHostAddr(4), system.MemberStateJoined),
				system.NewMember(system.Rank(7), common.MockUUID(7), "", common.MockHostAddr(4), system.MemberStateJoined),
			},
			expRanks:       "0-5",
			expAbsentHosts: "10.0.0.5",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer()
			cs := mockControlService(t, log, cfg, nil, nil, nil)
			cs.srvCfg = cfg
			cs.srvCfg.ControlPort = 10001
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			var expErr error
			if tc.expErrMsg != "" {
				expErr = errors.New(tc.expErrMsg)
			}
			gotResp, gotRankSet, gotErr := cs.rpcFanout(ctx, tc.fanReq, true)
			common.CmpErr(t, expErr, gotErr)
			if tc.expErrMsg != "" {
				return
			}

			cmpOpts := []cmp.Option{cmpopts.IgnoreUnexported(system.MemberResult{}, system.Member{})}
			if diff := cmp.Diff(tc.expResults, gotResp.Results, cmpOpts...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResp.Results, name)

			if diff := cmp.Diff(tc.expMembers, cs.membership.Members(nil), cmpOpts...); diff != "" {
				t.Logf("unexpected members (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expMembers, cs.membership.Members(nil), name)

			if diff := cmp.Diff(tc.expRanks, gotRankSet.String(), common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected ranks (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expAbsentHosts, gotResp.AbsentHosts.String(), "absent hosts")
			common.AssertEqual(t, tc.expAbsentRanks, gotResp.AbsentRanks.String(), "absent ranks")
		})
	}
}

func TestServer_CtlSvc_SystemQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		missingMembership bool
		nilReq            bool
		ranks             string
		hosts             string
		members           system.Members
		mResps            []*control.HostResponse
		expMembers        []*ctlpb.SystemMember
		expRanks          string
		expAbsentHosts    string
		expAbsentRanks    string
		expErrMsg         string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil *ctl.SystemQueryReq request",
		},
		"unfiltered rank results": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopping),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateReady),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateAwaitFormat),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateStopping),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "couldn't ping",
								State: uint32(system.MemberStateErrored),
							},
							{Rank: 1, State: uint32(system.MemberStateReady)},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 2, State: uint32(system.MemberStateUnresponsive)},
							{Rank: 3, State: uint32(system.MemberStateReady)},
						},
					},
				},
				{
					Addr: common.MockHostAddr(3).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 4, State: uint32(system.MemberStateStarting)},
							{Rank: 5, State: uint32(system.MemberStateStopped)},
						},
					},
				},
			},
			expMembers: []*ctlpb.SystemMember{
				{
					Rank: 0, Addr: common.MockHostAddr(1).String(),
					Uuid:  common.MockUUID(0),
					State: uint32(system.MemberStateErrored), Info: "couldn't ping",
					FaultDomain: "/",
				},
				{
					Rank: 1, Addr: common.MockHostAddr(1).String(),
					Uuid: common.MockUUID(1),
					// transition to "ready" illegal
					State:       uint32(system.MemberStateStopping),
					FaultDomain: "/",
				},
				{
					Rank: 2, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(2),
					State:       uint32(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(3),
					State:       uint32(system.MemberStateJoined),
					FaultDomain: "/",
				},
				{
					Rank: 4, Addr: common.MockHostAddr(3).String(),
					Uuid:        common.MockUUID(4),
					State:       uint32(system.MemberStateStarting),
					FaultDomain: "/",
				},
				{
					Rank: 5, Addr: common.MockHostAddr(3).String(),
					Uuid:        common.MockUUID(5),
					State:       uint32(system.MemberStateStopped),
					FaultDomain: "/",
				},
			},
			expRanks: "0-5",
		},
		"filtered and oversubscribed ranks": {
			ranks: "0,2-3,6-9",
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopping),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateReady),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateAwaitFormat),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateStopping),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "couldn't ping",
								State: uint32(system.MemberStateErrored),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 2, State: uint32(system.MemberStateUnresponsive)},
							{Rank: 3, State: uint32(system.MemberStateReady)},
						},
					},
				},
			},
			expMembers: []*ctlpb.SystemMember{
				{
					Rank: 0, Addr: common.MockHostAddr(1).String(),
					Uuid:  common.MockUUID(0),
					State: uint32(system.MemberStateErrored), Info: "couldn't ping",
					FaultDomain: "/",
				},
				{
					Rank: 2, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(2),
					State:       uint32(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(3),
					State:       uint32(system.MemberStateJoined),
					FaultDomain: "/",
				},
			},
			expRanks:       "0-5",
			expAbsentRanks: "6-9",
		},
		"filtered and oversubscribed hosts": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopping),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateReady),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateAwaitFormat),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateStopping),
			},
			hosts: "10.0.0.[2-5]",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 2, State: uint32(system.MemberStateUnresponsive)},
							{Rank: 3, State: uint32(system.MemberStateReady)},
						},
					},
				},
				{
					Addr: common.MockHostAddr(3).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 4, Errored: true, Msg: "couldn't ping",
								State: uint32(system.MemberStateErrored),
							},
							{Rank: 5, State: uint32(system.MemberStateReady)},
						},
					},
				},
			},
			expMembers: []*ctlpb.SystemMember{
				{
					Rank: 2, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(2),
					State:       uint32(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(3),
					State:       uint32(system.MemberStateJoined),
					FaultDomain: "/",
				},
				{
					Rank: 4, Addr: common.MockHostAddr(3).String(),
					Uuid:  common.MockUUID(4),
					State: uint32(system.MemberStateErrored), Info: "couldn't ping",
					FaultDomain: "/",
				},
				{
					Rank: 5, Addr: common.MockHostAddr(3).String(),
					Uuid:        common.MockUUID(5),
					State:       uint32(system.MemberStateStopping),
					FaultDomain: "/",
				},
			},
			expRanks:       "2-5",
			expAbsentHosts: "10.0.0.[4-5]",
		},
		"missing hosts": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopping),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateReady),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(4), common.MockUUID(4), "", common.MockHostAddr(3), system.MemberStateAwaitFormat),
				system.NewMember(system.Rank(5), common.MockUUID(5), "", common.MockHostAddr(3), system.MemberStateStopping),
			},
			hosts:          "10.0.0.[4-5]",
			expRanks:       "",
			expAbsentHosts: "10.0.0.[4-5]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer()
			cs := mockControlService(t, log, cfg, nil, nil, nil)
			cs.srvCfg = cfg
			cs.srvCfg.ControlPort = 10001
			cs.membership = system.MockMembership(t, log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(t, log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			cs.harness.instances[0]._superblock.Rank = system.NewRankPtr(0)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			req := &ctlpb.SystemQueryReq{Ranks: tc.ranks, Hosts: tc.hosts}
			if tc.nilReq {
				req = nil
			}

			gotResp, gotErr := cs.SystemQuery(ctx, req)
			common.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			if diff := cmp.Diff(tc.expMembers, gotResp.Members, common.DefaultCmpOpts()...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expMembers, gotResp.Members, name)
			common.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			common.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")
		})
	}
}

func TestServer_CtlSvc_SystemStart(t *testing.T) {
	for name, tc := range map[string]struct {
		nilReq         bool
		ranks          string
		hosts          string
		members        system.Members
		mResps         []*control.HostResponse
		expMembers     system.Members
		expResults     []*ctlpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expErrMsg      string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil *ctl.SystemStartReq request",
		},
		"unfiltered rank results": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "couldn't start",
								State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 1, State: uint32(system.MemberStateReady),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, State: uint32(system.MemberStateReady),
							},
							{
								Rank: 3, State: uint32(system.MemberStateReady),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "start", Errored: true,
					Msg: "couldn't start", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: "start", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateReady),
				},
				{
					Rank: 2, Action: "start", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateReady),
				},
				{
					Rank: 3, Action: "start", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateReady),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateReady),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateReady),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateReady),
			},
		},
		"filtered and oversubscribed ranks": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			ranks: "0-1,4-9",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "couldn't start",
								State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 1, State: uint32(system.MemberStateReady),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "start", Errored: true,
					Msg: "couldn't start", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: "start", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateReady),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			expAbsentRanks: "4-9",
		},
		"filtered and oversubscribed hosts": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			hosts: "10.0.0.[2-5]",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, Errored: true, Msg: "couldn't start",
								State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 3, State: uint32(system.MemberStateReady),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 2, Action: "start", Errored: true,
					Msg: "couldn't start", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 3, Action: "start", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateReady),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateReady),
			},
			expAbsentHosts: "10.0.0.[3-5]",
		},
		"filtered hosts": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
			hosts: "10.0.0.[1-2]",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, State: uint32(system.MemberStateReady),
							},
							{
								Rank: 1, State: uint32(system.MemberStateReady),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, State: uint32(system.MemberStateReady),
							},
							{
								Rank: 3, State: uint32(system.MemberStateReady),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "start", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateReady),
				},
				{
					Rank: 1, Action: "start", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateReady),
				},
				{
					Rank: 2, Action: "start", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateReady),
				},
				{
					Rank: 3, Action: "start", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateReady),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(2), "", common.MockHostAddr(2), system.MemberStateReady),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer()
			cs := mockControlService(t, log, cfg, nil, nil, nil)
			cs.srvCfg = cfg
			cs.srvCfg.ControlPort = 10001
			cs.membership = system.MockMembership(t, log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(t, log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			cs.harness.instances[0]._superblock.Rank = system.NewRankPtr(0)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			req := &ctlpb.SystemStartReq{Ranks: tc.ranks, Hosts: tc.hosts}
			if tc.nilReq {
				req = nil
			}

			gotResp, gotErr := cs.SystemStart(ctx, req)
			common.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			if diff := cmp.Diff(tc.expResults, gotResp.Results, common.DefaultCmpOpts()...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResp.Results, name)
			common.AssertEqual(t, tc.expMembers, cs.membership.Members(nil), name)
			common.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			common.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")
		})
	}
}

func TestServer_CtlSvc_SystemStop(t *testing.T) {
	for name, tc := range map[string]struct {
		req            *ctlpb.SystemStopReq
		members        system.Members
		mResps         []*control.HostResponse
		expMembers     system.Members
		expResults     []*ctlpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expErrMsg      string
	}{
		"nil req": {
			req:       (*ctlpb.SystemStopReq)(nil),
			expErrMsg: "nil *ctl.SystemStopReq request",
		},
		"invalid req": {
			req:       new(ctlpb.SystemStopReq),
			expErrMsg: "response results not populated",
		},
		"unfiltered prep fail": {
			req: &ctlpb.SystemStopReq{Prep: true, Kill: true},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "prep shutdown failed",
								State: uint32(system.MemberStateJoined),
							},
							{
								Rank: 1, State: uint32(system.MemberStateStopping),
							},
						},
					},
				},
			},
			expErrMsg: "PrepShutdown HasErrors",
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "prep shutdown", Errored: true,
					Msg: "prep shutdown failed", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateJoined),
				},
				{
					Rank: 1, Action: "prep shutdown", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopping),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopping),
			},
		},
		"filtered and oversubscribed ranks prep fail": {
			req: &ctlpb.SystemStopReq{Prep: true, Kill: true, Ranks: "0-1,9"},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "prep shutdown failed",
								State: uint32(system.MemberStateJoined),
							},
							{
								Rank: 1, State: uint32(system.MemberStateStopping),
							},
						},
					},
				},
			},
			expErrMsg: "PrepShutdown HasErrors",
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "prep shutdown", Errored: true,
					Msg: "prep shutdown failed", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateJoined),
				},
				{
					Rank: 1, Action: "prep shutdown", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopping),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopping),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
			expAbsentRanks: "9",
		},
		"filtered and oversubscribed hosts prep fail": {
			req: &ctlpb.SystemStopReq{Prep: true, Kill: true, Hosts: "10.0.0.[1,3]"},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "prep shutdown failed",
								State: uint32(system.MemberStateJoined),
							},
							{
								Rank: 1, State: uint32(system.MemberStateStopping),
							},
						},
					},
				},
			},
			expErrMsg: "PrepShutdown HasErrors",
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "prep shutdown", Errored: true,
					Msg: "prep shutdown failed", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateJoined),
				},
				{
					Rank: 1, Action: "prep shutdown", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopping),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopping),
				system.NewMember(system.Rank(3), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
			expAbsentHosts: "10.0.0.3",
		},
		"unfiltered rank results": {
			req: &ctlpb.SystemStopReq{Prep: false, Kill: true},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateJoined),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "couldn't stop",
								State: uint32(system.MemberStateJoined),
							},
							{
								Rank: 1, State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 3, State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "stop", Errored: true,
					Msg: "couldn't stop", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateJoined),
				},
				{
					Rank: 1, Action: "stop", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 2, Action: "stop", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 3, Action: "stop", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
		},
		"filtered and oversubscribed ranks": {
			req: &ctlpb.SystemStopReq{Prep: false, Kill: true, Ranks: "0,2,3-9"},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "couldn't stop",
								State: uint32(system.MemberStateJoined),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 3, State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "stop", Errored: true,
					Msg: "couldn't stop", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateJoined),
				},
				{
					Rank: 2, Action: "stop", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 3, Action: "stop", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			expAbsentRanks: "4-9",
		},
		"filtered and oversubscribed hosts": {
			req: &ctlpb.SystemStopReq{Prep: false, Kill: true, Hosts: "10.0.0.[2-5]"},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 3, State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 2, Action: "stop", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 3, Action: "stop", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			expAbsentHosts: "10.0.0.[3-5]",
		},
		"filtered hosts": {
			req: &ctlpb.SystemStopReq{Prep: false, Kill: true, Hosts: "10.0.0.[1-2]"},
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 1, State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 3, State: uint32(system.MemberStateStopped),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "stop", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: "stop", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 2, Action: "stop", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 3, Action: "stop", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer()
			cs := mockControlService(t, log, cfg, nil, nil, nil)
			cs.srvCfg = cfg
			cs.srvCfg.ControlPort = 10001
			cs.membership = system.MockMembership(t, log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(t, log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			cs.harness.instances[0]._superblock.Rank = system.NewRankPtr(0)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			gotResp, gotErr := cs.SystemStop(ctx, tc.req)
			common.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" && tc.expErrMsg != "PrepShutdown HasErrors" {
				return
			}

			if diff := cmp.Diff(tc.expResults, gotResp.Results, common.DefaultCmpOpts()...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResp.Results, name)

			for _, m := range tc.expMembers {
				member, err := cs.membership.Get(m.Rank)
				if err != nil {
					t.Fatal(err)
				}
				t.Logf("got %#v, want %#v", member, m)
				common.AssertEqual(t, m.State().String(), member.State().String(),
					name+": compare state rank"+m.Rank.String())
			}

			common.AssertEqual(t, tc.expMembers, cs.membership.Members(nil), name)
			common.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			common.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")
		})
	}
}

func TestServer_CtlSvc_SystemResetFormat(t *testing.T) {
	for name, tc := range map[string]struct {
		nilReq         bool
		ranks          string
		hosts          string
		members        system.Members
		mResps         []*control.HostResponse
		expMembers     system.Members
		expResults     []*ctlpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expErrMsg      string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil *ctl.SystemResetFormatReq request",
		},
		"unfiltered rank results": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "something bad",
								State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 1, State: uint32(system.MemberStateAwaitFormat),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, State: uint32(system.MemberStateAwaitFormat),
							},
							{
								Rank: 3, State: uint32(system.MemberStateAwaitFormat),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "reset format", Errored: true,
					Msg: "something bad", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: "reset format", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
				{
					Rank: 2, Action: "reset format", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
				{
					Rank: 3, Action: "reset format", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
			},

			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateAwaitFormat),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateAwaitFormat),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateAwaitFormat),
			},
		},
		"filtered and oversubscribed ranks": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			ranks: "0-1,4-9",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 0, Errored: true, Msg: "couldn't reset",
								State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 1, State: uint32(system.MemberStateAwaitFormat),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 0, Action: "reset format", Errored: true,
					Msg: "couldn't reset", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: "reset format", Addr: common.MockHostAddr(1).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateAwaitFormat),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			expAbsentRanks: "4-9",
		},
		"filtered and oversubscribed hosts": {
			members: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateStopped),
			},
			hosts: "10.0.0.[2-5]",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{
								Rank: 2, Errored: true, Msg: "couldn't reset",
								State: uint32(system.MemberStateStopped),
							},
							{
								Rank: 3, State: uint32(system.MemberStateAwaitFormat),
							},
						},
					},
				},
			},
			expResults: []*ctlpb.RankResult{
				{
					Rank: 2, Action: "reset format", Errored: true,
					Msg: "couldn't reset", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 3, Action: "reset format", Addr: common.MockHostAddr(2).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
			},
			expMembers: system.Members{
				system.NewMember(system.Rank(0), common.MockUUID(0), "", common.MockHostAddr(1), system.MemberStateStopped),
				system.NewMember(system.Rank(1), common.MockUUID(1), "", common.MockHostAddr(1), system.MemberStateJoined),
				system.NewMember(system.Rank(2), common.MockUUID(3), "", common.MockHostAddr(2), system.MemberStateStopped),
				system.NewMember(system.Rank(3), common.MockUUID(4), "", common.MockHostAddr(2), system.MemberStateAwaitFormat),
			},
			expAbsentHosts: "10.0.0.[3-5]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer()
			cs := mockControlService(t, log, cfg, nil, nil, nil)
			cs.srvCfg = cfg
			cs.srvCfg.ControlPort = 10001
			cs.membership = system.MockMembership(t, log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(t, log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			cs.harness.instances[0]._superblock.Rank = system.NewRankPtr(0)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			req := &ctlpb.SystemResetFormatReq{Ranks: tc.ranks, Hosts: tc.hosts}
			if tc.nilReq {
				req = nil
			}

			gotResp, gotErr := cs.SystemResetFormat(ctx, req)
			common.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			if diff := cmp.Diff(tc.expResults, gotResp.Results, common.DefaultCmpOpts()...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResp.Results, name)

			common.AssertEqual(t, tc.expMembers, cs.membership.Members(nil), name)
			common.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			common.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")
		})
	}
}
