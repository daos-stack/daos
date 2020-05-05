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

func TestServer_CtlSvc_getMSMember(t *testing.T) {
	mockMember1 := system.NewMember(1, "", getHostAddr(1), system.MemberStateStopped)
	mockMember2 := system.NewMember(2, "", getHostAddr(2), system.MemberStateStopped)

	for name, tc := range map[string]struct {
		hasMembership bool
		members       system.Members
		hasAP         bool
		hasRank       bool
		expMember     *system.Member
		expErrMsg     string
	}{
		"no membership": {expErrMsg: "no membership found on host"},
		"no access point": {
			hasMembership: true,
			expErrMsg:     "get MS instance: instance is not an access point, try " + defaultAP,
		},
		"no rank": {
			hasMembership: true,
			hasAP:         true,
			expErrMsg:     "nil rank in superblock",
		},
		"missing member": {
			hasMembership: true,
			members:       system.Members{mockMember2},
			hasAP:         true,
			hasRank:       true,
			expErrMsg:     system.FaultMemberMissing(1).Error(),
		},
		"success": {
			hasMembership: true,
			members:       system.Members{mockMember2, mockMember1},
			hasAP:         true,
			hasRank:       true,
			expMember:     mockMember1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			if tc.hasMembership {
				cs.membership = system.NewMembership(log)
				for _, m := range tc.members {
					if _, err := cs.membership.Add(m); err != nil {
						t.Fatal(err)
					}
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			for _, srv := range cs.harness.instances {
				m := newMgmtSvcClient(
					context.Background(), log, mgmtSvcClientCfg{
						AccessPoints: []string{defaultAP},
					},
				)
				srv.msClient = m

				if tc.hasAP {
					srv._superblock.MS = true
				}
				if tc.hasRank {
					srv._superblock.Rank = system.NewRankPtr(srv.Index() + 1)
				}
			}

			gotMember, gotErr := cs.getMSMember()
			common.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			common.AssertEqual(t, tc.expMember, gotMember, name)
		})
	}
}

func TestServer_CtlSvc_filterRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		rankList  []system.Rank
		excludeMS bool
		expRanks  []system.Rank
	}{
		"exclude ms leader rank": {
			excludeMS: true,
			rankList:  []system.Rank{0, 1, 2, 3},
			expRanks:  []system.Rank{1, 2, 3},
		},
		"include ms leader rank": {
			rankList: []system.Rank{0, 1, 2, 3},
			expRanks: []system.Rank{0, 1, 2, 3},
		},
		"empty rank list": {
			rankList: []system.Rank{},
			expRanks: []system.Rank{0, 1, 2, 3},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			cs.membership = system.NewMembership(log)
			for _, r := range []uint32{0, 1, 2, 3} {
				if _, err := cs.membership.Add(
					system.NewMember(system.Rank(r), "", &net.TCPAddr{},
						system.MemberStateStopped),
				); err != nil {

					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(log, maxIOServers, false)
			cs.harness = mgmtSvc.harness
			cs.harness.started.SetTrue()
			for _, srv := range cs.harness.instances {
				m := newMgmtSvcClient(
					context.Background(), log, mgmtSvcClientCfg{
						AccessPoints: []string{defaultAP},
					},
				)
				srv.msClient = m

				srv._superblock.MS = true
				srv._superblock.Rank = system.NewRankPtr(srv.Index())
			}

			gotRanks, gotErr := cs.filterRanks(tc.rankList, tc.excludeMS)
			if gotErr != nil {
				t.Fatal(gotErr)
			}

			common.AssertEqual(t, tc.expRanks, gotRanks, name)
		})
	}
}

func TestServer_CtlSvc_rpcToRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		members    system.Members
		ranks      []system.Rank
		mic        *control.MockInvokerConfig
		hostErrors control.HostErrorsMap
		expHosts   []string
		expResults system.MemberResults
		expErrMsg  string
	}{
		"empty rank list": {
			ranks:     []system.Rank{},
			expErrMsg: "no ranks specified in request",
		},
		"success": {
			members: system.Members{
				system.NewMember(0, "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(1, "", getHostAddr(2), system.MemberStateJoined),
				system.NewMember(2, "", getHostAddr(2), system.MemberStateJoined),
				system.NewMember(3, "", getHostAddr(1), system.MemberStateJoined),
				system.NewMember(4, "", getHostAddr(3), system.MemberStateJoined),
				system.NewMember(5, "", getHostAddr(3), system.MemberStateJoined),
				system.NewMember(6, "", getHostAddr(4), system.MemberStateJoined),
				system.NewMember(7, "", getHostAddr(4), system.MemberStateJoined),
			},
			ranks: []system.Rank{0, 1, 2, 3, 6, 7},
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr: getHostAddr(1).String(),
							Message: &mgmtpb.RanksResp{
								Results: []*mgmtpb.RanksResp_RankResult{
									{
										Rank: 0, Action: "start",
										Errored: true, Msg: "fatality",
										State: uint32(system.MemberStateErrored),
									},
									{
										Rank: 3, Action: "start",
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
										Rank: 1, Action: "start",
										State: uint32(system.MemberStateJoined),
									},
									{
										Rank: 2, Action: "start",
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
				},
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			// results from ranks outside rank list absent
			expResults: system.MemberResults{
				{
					Rank: 0, Action: "start",
					Errored: true, Msg: "fatality",
					State: system.MemberStateErrored,
				},
				{
					Rank: 3, Action: "start",
					State: system.MemberStateJoined,
				},
				{
					Rank: 1, Action: "start",
					State: system.MemberStateJoined,
				},
				{
					Rank: 2, Action: "start",
					State: system.MemberStateJoined,
				},
				{
					Rank: 6, Action: "start",
					Errored: true, Msg: "connection refused",
					State: system.MemberStateStopped,
				},
				{
					Rank: 7, Action: "start",
					Errored: true, Msg: "connection refused",
					State: system.MemberStateStopped,
				},
			},
			// hosts containing any of the ranks (filtered by rank list)
			expHosts: []string{
				getHostAddr(1).String(), getHostAddr(2).String(), getHostAddr(4).String(),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			cs.membership = system.NewMembership(log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mic := tc.mic
			if mic == nil {
				mic = control.DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, mic)
			cs.rpcClient = mi

			req := &control.RanksReq{Ranks: tc.ranks}
			// use StartRanks as an example control API RPC, applies
			// to {Stop,Ping,PrepShutdown}Ranks
			gotResults, gotErr := cs.rpcToRanks(ctx, req, start)
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

func TestServer_CtlSvc_SystemStart(t *testing.T) {
	for name, tc := range map[string]struct {
		ranks      []uint32
		members    system.Members
		mic        *control.MockInvokerConfig
		expMembers system.Members
		expResp    *ctlpb.SystemStartResp
		expErr     error
	}{
		"multiple node mixed rank results": {
			members: system.Members{
				system.NewMember(0, "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(2, "", getHostAddr(2), system.MemberStateStopped),
				system.NewMember(3, "", getHostAddr(2), system.MemberStateStopped),
			},
			mic: &control.MockInvokerConfig{
				UnaryResponse: &control.UnaryResponse{
					Responses: []*control.HostResponse{
						{
							Addr: getHostAddr(1).String(),
							Message: &mgmtpb.RanksResp{
								Results: []*mgmtpb.RanksResp_RankResult{
									{
										Rank: 0, Action: "start",
										Errored: true, Msg: "couldn't start",
										State: uint32(system.MemberStateStopped),
									},
									{
										Rank: 1, Action: "start",
										State: uint32(system.MemberStateReady),
									},
								},
							},
						},
						{
							Addr: getHostAddr(2).String(),
							Message: &mgmtpb.RanksResp{
								Results: []*mgmtpb.RanksResp_RankResult{
									{
										Rank: 2, Action: "start",
										State: uint32(system.MemberStateReady),
									},
									{
										Rank: 3, Action: "start",
										State: uint32(system.MemberStateReady),
									},
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SystemStartResp{
				Results: []*ctlpb.RankResult{
					{
						Rank: 0, Action: "start", Errored: true,
						Msg:   "couldn't start",
						State: uint32(system.MemberStateStopped),
					},
					{Rank: 1, Action: "start", State: uint32(system.MemberStateReady)},
					{Rank: 2, Action: "start", State: uint32(system.MemberStateReady)},
					{Rank: 3, Action: "start", State: uint32(system.MemberStateReady)},
				},
			},
			expMembers: system.Members{
				system.NewMember(0, "", getHostAddr(1), system.MemberStateStopped),
				system.NewMember(1, "", getHostAddr(1), system.MemberStateReady),
				system.NewMember(2, "", getHostAddr(2), system.MemberStateReady),
				system.NewMember(3, "", getHostAddr(2), system.MemberStateReady),
			},
		},
		//		"rank list miss": {
		//			ranks: []uint32{1, 2, 3},
		//			mic: &control.MockInvokerConfig{
		//				UnaryResponse: control.MockMSResponse("host1", nil,
		//					&mgmtpb.RanksResp{
		//						Results: []*mgmtpb.RanksResp_RankResult{
		//							{
		//								Rank: 0, Action: "start",
		//								Errored: true, Msg: "fatality",
		//								State: uint32(system.MemberStateErrored),
		//							},
		//							{
		//								Rank: 1, Action: "start",
		//								State: uint32(system.MemberStateJoined),
		//							},
		//						},
		//					},
		//				),
		//			},
		//			//			expResp: &ctlpb.SystemStartResp{
		//			//				Results: []*ctlpb.RankResult{
		//			//					{
		//			//						Rank: 0, Action: "start", Errored: true, Msg: "fatality",
		//			//						State: uint32(system.MemberStateErrored),
		//			//					},
		//			//					{Rank: 1, Action: "start", State: uint32(system.MemberStateJoined)},
		//			//				},
		//			//			},
		//		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			emptyCfg := emptyMockConfig(t)
			cs := mockControlService(t, log, emptyCfg, nil, nil, nil)
			cs.membership = system.NewMembership(log)
			for _, m := range tc.members {
				if _, err := cs.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			mgmtSvc := newTestMgmtSvcMulti(log, maxIOServers, false)
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

			mic := tc.mic
			if mic == nil {
				mic = control.DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := control.NewMockInvoker(log, mic)
			cs.rpcClient = mi

			gotResp, gotErr := cs.SystemStart(ctx, &ctlpb.SystemStartReq{Ranks: tc.ranks})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResp, gotResp, name)

			//			if diff := cmp.Diff(tc.expMembers, gotMembers, common.DefaultCmpOpts()...); diff != "" {
			//				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			//			}
			common.AssertEqual(t, tc.expMembers, cs.membership.Members([]system.Rank{}), name)
		})
	}
}
