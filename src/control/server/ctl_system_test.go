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

// +build never // disable temporarily

package server

import (
	"fmt"
	"net"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

const defaultAP = "192.168.1.1:10001"

var hostAddrs = make(map[int]*net.TCPAddr)

func getHostAddr(i int) *net.TCPAddr {
	if _, exists := hostAddrs[i]; !exists {
		addr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("10.0.0.%d:10001", i))
		if err != nil {
			panic(err)
		}
		hostAddrs[i] = addr
	}
	return hostAddrs[i]
}

func TestServer_CtlSvc_rpcFanout(t *testing.T) {
	defaultMembers := system.Members{
		system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateJoined),
		system.NewMember(1, common.MockUUID(1), "", getHostAddr(2), system.MemberStateJoined),
		system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateJoined),
		system.NewMember(3, common.MockUUID(3), "", getHostAddr(1), system.MemberStateJoined),
		system.NewMember(4, common.MockUUID(4), "", getHostAddr(3), system.MemberStateJoined),
		system.NewMember(5, common.MockUUID(5), "", getHostAddr(3), system.MemberStateJoined),
		system.NewMember(6, common.MockUUID(6), "", getHostAddr(4), system.MemberStateJoined),
		system.NewMember(7, common.MockUUID(7), "", getHostAddr(4), system.MemberStateJoined),
	}

	for name, tc := range map[string]struct {
		missingMembership bool
		members           system.Members
		ranks             []system.Rank
		mResps            []*control.HostResponse
		hostErrors        control.HostErrorsMap
		expHosts          []string
		expResults        system.MemberResults
		expErrMsg         string
	}{
		"no membership": {
			missingMembership: true,
			expErrMsg:         "nil system membership",
		},
		"no members": {
			members:   system.Members{},
			expErrMsg: "empty system membership",
		},
		"unfiltered ranks": {
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Addr: getHostAddr(2).String(),
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
					Addr:  getHostAddr(3).String(),
					Error: errors.New("connection refused"),
				},
				{
					Addr:  getHostAddr(4).String(),
					Error: errors.New("connection refused"),
				},
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			expResults: system.MemberResults{
				{
					Rank: 0, Errored: true, Msg: "fatality",
					State: system.MemberStateErrored,
				},
				{Rank: 3, State: system.MemberStateJoined},
				{Rank: 1, State: system.MemberStateJoined},
				{Rank: 2, State: system.MemberStateJoined},
				{
					Rank: 4, Msg: "connection refused",
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 5, Msg: "connection refused",
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 6, Msg: "connection refused",
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 7, Msg: "connection refused",
					State: system.MemberStateUnresponsive,
				},
			},
			// all hosts because // of empty rank list
			expHosts: []string{
				"", getHostAddr(1).String(), "", getHostAddr(2).String(),
				"", getHostAddr(3).String(), "", getHostAddr(4).String(),
			},
		},
		"filtered ranks": {
			ranks: []system.Rank{0, 1, 2, 3, 6, 7},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Addr: getHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 1, State: uint32(system.MemberStateJoined)},
							{Rank: 2, State: uint32(system.MemberStateJoined)},
						},
					},
				},
				{
					Addr:  getHostAddr(4).String(),
					Error: errors.New("connection refused"),
				},
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			// results from ranks outside rank list absent
			expResults: system.MemberResults{
				{
					Rank: 0, Errored: true, Msg: "fatality",
					State: system.MemberStateErrored,
				},
				{Rank: 3, State: system.MemberStateJoined},
				{Rank: 1, State: system.MemberStateJoined},
				{Rank: 2, State: system.MemberStateJoined},
				{
					Rank: 6, Msg: "connection refused",
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 7, Msg: "connection refused",
					State: system.MemberStateUnresponsive,
				},
			},
			// hosts containing any of the ranks (filtered by rank list)
			expHosts: []string{
				"", getHostAddr(1).String(), "", getHostAddr(2).String(), "", getHostAddr(4).String(),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			if !tc.missingMembership {
				cs.membership = system.MockMembership(t, log)

				if tc.members == nil {
					tc.members = defaultMembers
				}
				for _, m := range tc.members {
					if _, err := cs.membership.Add(m); err != nil {
						t.Fatal(err)
					}
				}
			}

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			req := &control.RanksReq{Ranks: tc.ranks}
			gotResults, gotErr := cs.rpcFanout(ctx, req, start)
			common.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			common.AssertStringsEqual(t, tc.expHosts, req.HostList, name)

			if diff := cmp.Diff(tc.expResults, gotResults, common.DefaultCmpOpts()...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResults, name)
		})
	}
}

func TestServer_CtlSvc_SystemQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		nilReq     bool
		ranks      []uint32
		members    system.Members
		mResps     []*control.HostResponse
		expMembers []*ctlpb.SystemMember
		expErrMsg  string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil request",
		},
		"empty membership": {
			members: system.Members{},
		},
		"unfiltered rank results": {
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateStopping),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateReady),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateJoined),
				system.NewMember(4, common.MockUUID(4), "", getHostAddr(3), system.MemberStateAwaitFormat),
				system.NewMember(5, common.MockUUID(5), "", getHostAddr(3), system.MemberStateStopping),
			},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Addr: getHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 2, State: uint32(system.MemberStateUnresponsive)},
							{Rank: 3, State: uint32(system.MemberStateReady)},
						},
					},
				},
				{
					Addr: getHostAddr(3).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 4, State: uint32(system.MemberStateStarting)},
							{Rank: 5, State: uint32(system.MemberStateStopped)},
						},
					},
				},
			},
			expMembers: []*ctlpb.SystemMember{
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(0),
					Rank: 0, Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateErrored), Info: "couldn't ping",
				},
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(1),
					Rank: 1, Addr: getHostAddr(1).String(),
					// transition to "ready" illegal
					State: uint32(system.MemberStateStopping),
				},
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(2),
					Rank: 2, Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateUnresponsive),
				},
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(3),
					Rank: 3, Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateJoined),
				},
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(4),
					Rank: 4, Addr: getHostAddr(3).String(),
					State: uint32(system.MemberStateStarting),
				},
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(5),
					Rank: 5, Addr: getHostAddr(3).String(),
					State: uint32(system.MemberStateStopped),
				},
			},
		},
		"filtered rank results": {
			ranks: []uint32{0, 2, 3},
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateStopping),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateReady),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateJoined),
				system.NewMember(4, common.MockUUID(4), "", getHostAddr(3), system.MemberStateAwaitFormat),
				system.NewMember(5, common.MockUUID(5), "", getHostAddr(3), system.MemberStateStopping),
			},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Addr: getHostAddr(2).String(),
					Message: &mgmtpb.RanksResp{
						Results: []*mgmtpb.RanksResp_RankResult{
							{Rank: 2, State: uint32(system.MemberStateUnresponsive)},
							{Rank: 3, State: uint32(system.MemberStateReady)},
						},
					},
				},
			},
			expMembers: []*ctlpb.SystemMember{
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(0),
					Rank: 0, Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateErrored), Info: "couldn't ping",
				},
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(2),
					Rank: 2, Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateUnresponsive),
				},
				&ctlpb.SystemMember{
					Uuid: common.MockUUID(3),
					Rank: 3, Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateJoined),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			cs.membership = system.MockMembership(t, log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(t, log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			m := newMgmtSvcClient(
				context.Background(), log, mgmtSvcClientCfg{
					AccessPoints: []string{defaultAP},
				},
			)
			cs.harness.instances[0].msClient = m
			cs.harness.instances[0]._superblock.MS = true
			cs.harness.instances[0]._superblock.Rank = system.NewRankPtr(0)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			req := &ctlpb.SystemQueryReq{Ranks: tc.ranks}
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
		})
	}
}

func TestServer_CtlSvc_SystemStart(t *testing.T) {
	for name, tc := range map[string]struct {
		nilReq     bool
		ranks      []uint32
		members    system.Members
		mResps     []*control.HostResponse
		expMembers system.Members
		expResults []*ctlpb.RankResult
		expErrMsg  string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil request",
		},
		"unfiltered rank results": {
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateStopped),
			},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Addr: getHostAddr(2).String(),
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
					Rank: 0, Action: string(start), Errored: true,
					Msg: "couldn't start", Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: string(start), Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateReady),
				},
				{
					Rank: 2, Action: string(start), Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateReady),
				},
				{
					Rank: 3, Action: string(start), Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateReady),
				},
			},

			expMembers: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateReady),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateReady),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateReady),
			},
		},
		"filtered rank results": {
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateStopped),
			},
			ranks: []uint32{0, 1},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Rank: 0, Action: string(start), Errored: true,
					Msg: "couldn't start", Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: string(start), Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateReady),
				},
			},
			expMembers: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateStopped),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			cs.membership = system.MockMembership(t, log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(t, log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			m := newMgmtSvcClient(
				context.Background(), log, mgmtSvcClientCfg{
					AccessPoints: []string{defaultAP},
				},
			)
			cs.harness.instances[0].msClient = m
			cs.harness.instances[0]._superblock.MS = true
			cs.harness.instances[0]._superblock.Rank = system.NewRankPtr(0)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			req := &ctlpb.SystemStartReq{Ranks: tc.ranks}
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

			common.AssertEqual(t, tc.expMembers, cs.membership.Members(), name)
		})
	}
}

func TestServer_CtlSvc_SystemStop(t *testing.T) {
	for name, tc := range map[string]struct {
		req        *ctlpb.SystemStopReq
		members    system.Members
		mResps     []*control.HostResponse
		expMembers system.Members
		expResults []*ctlpb.RankResult
		expErrMsg  string
	}{
		"nil req": {
			expErrMsg: "nil request",
		},
		"invalid req": {
			req:       &ctlpb.SystemStopReq{},
			expErrMsg: "response results not populated",
		},
		"prep fail": {
			req: &ctlpb.SystemStopReq{Prep: true, Kill: true},
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
		},
		"unfiltered rank results": {
			req: &ctlpb.SystemStopReq{Prep: false, Kill: true},
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateJoined),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Addr: getHostAddr(2).String(),
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
					Rank: 0, Action: string(stop), Errored: true,
					Msg: "couldn't stop", Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateJoined),
				},
				{
					Rank: 1, Action: string(stop), Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 2, Action: string(stop), Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 3, Action: string(stop), Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
			},
			expMembers: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateStopped),
			},
		},
		"filtered rank results": {
			req: &ctlpb.SystemStopReq{Prep: false, Kill: true, Ranks: []uint32{0, 2, 3}},
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateJoined),
			},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Addr: getHostAddr(2).String(),
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
					Rank: 0, Action: string(stop), Errored: true,
					Msg: "couldn't stop", Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateJoined),
				},
				{
					Rank: 2, Action: string(stop), Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 3, Action: string(stop), Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateStopped),
				},
			},
			expMembers: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateStopped),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			cs.membership = system.MockMembership(t, log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(t, log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			m := newMgmtSvcClient(
				context.Background(), log, mgmtSvcClientCfg{
					AccessPoints: []string{defaultAP},
				},
			)
			cs.harness.instances[0].msClient = m
			cs.harness.instances[0]._superblock.MS = true
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
			if tc.expErrMsg != "" {
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

			common.AssertEqual(t, tc.expMembers, cs.membership.Members(), name)
		})
	}
}

func TestServer_CtlSvc_SystemResetFormat(t *testing.T) {
	for name, tc := range map[string]struct {
		nilReq     bool
		ranks      []uint32
		members    system.Members
		mResps     []*control.HostResponse
		expMembers system.Members
		expResults []*ctlpb.RankResult
		expErrMsg  string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil request",
		},
		"unfiltered rank results": {
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateStopped),
			},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Addr: getHostAddr(2).String(),
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
					Rank: 0, Action: string(reset), Errored: true,
					Msg: "something bad", Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: string(reset), Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
				{
					Rank: 2, Action: string(reset), Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
				{
					Rank: 3, Action: string(reset), Addr: getHostAddr(2).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
			},

			expMembers: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateAwaitFormat),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateAwaitFormat),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateAwaitFormat),
			},
		},
		"filtered rank results": {
			members: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateStopped),
			},
			ranks: []uint32{0, 1},
			mResps: []*control.HostResponse{
				{
					Addr: getHostAddr(1).String(),
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
					Rank: 0, Action: string(reset), Errored: true,
					Msg: "couldn't reset", Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateStopped),
				},
				{
					Rank: 1, Action: string(reset), Addr: getHostAddr(1).String(),
					State: uint32(system.MemberStateAwaitFormat),
				},
			},
			expMembers: system.Members{
				system.NewMember(0, common.MockUUID(0), "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, common.MockUUID(1), "", getHostAddr(1), system.MemberStateAwaitFormat),
				system.NewMember(2, common.MockUUID(2), "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, common.MockUUID(3), "", getHostAddr(2), system.MemberStateStopped),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			cs.membership = system.MockMembership(t, log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(t, log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			m := newMgmtSvcClient(
				context.Background(), log, mgmtSvcClientCfg{
					AccessPoints: []string{defaultAP},
				},
			)
			cs.harness.instances[0].msClient = m
			cs.harness.instances[0]._superblock.MS = true
			cs.harness.instances[0]._superblock.Rank = system.NewRankPtr(0)

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: tc.mResps,
				},
			})
			cs.rpcClient = mi

			req := &ctlpb.SystemResetFormatReq{Ranks: tc.ranks}
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

			common.AssertEqual(t, tc.expMembers, cs.membership.Members(), name)
		})
	}
}
