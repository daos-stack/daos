//
// (C) Copyright 2020-2021 Intel Corporation.
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
	"context"
	"net"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_MgmtSvc_LeaderQuery(t *testing.T) {
	localhost := common.LocalhostCtrlAddr()

	for name, tc := range map[string]struct {
		req     *mgmtpb.LeaderQueryReq
		expResp *mgmtpb.LeaderQueryResp
		expErr  error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"wrong system": {
			req: &mgmtpb.LeaderQueryReq{
				Sys: "quack",
			},
			expErr: FaultWrongSystem("quack", build.DefaultSystemName),
		},
		"successful query": {
			req: &mgmtpb.LeaderQueryReq{Sys: build.DefaultSystemName},
			expResp: &mgmtpb.LeaderQueryResp{
				CurrentLeader: localhost.String(),
				Replicas:      []string{localhost.String()},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mgmtSvc := newTestMgmtSvc(t, log)
			db, cleanup := system.TestDatabase(t, log)
			defer cleanup()
			mgmtSvc.sysdb = db

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			if err := db.Start(ctx); err != nil {
				t.Fatal(err)
			}

			// wait for the bootstrap to finish
			for {
				if leader, _, _ := db.LeaderQuery(); leader != "" {
					break
				}
			}

			gotResp, gotErr := mgmtSvc.LeaderQuery(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

type eventsDispatched struct {
	rx     []events.Event
	cancel context.CancelFunc
}

func (d *eventsDispatched) OnEvent(ctx context.Context, e events.Event) {
	d.rx = append(d.rx, e)
	d.cancel()
}

func TestServer_MgmtSvc_ClusterEvent(t *testing.T) {
	eventRankExit := events.NewRankExitEvent("foo", 0, 0, common.NormalExit)

	for name, tc := range map[string]struct {
		nilReq        bool
		zeroSeq       bool
		event         events.Event
		expResp       *mgmtpb.ClusterEventResp
		expDispatched []events.Event
		expErr        error
	}{
		"nil request": {
			nilReq: true,
			expErr: errors.New("nil request"),
		},
		"invalid sequence number": {
			zeroSeq: true,
			expErr:  errors.New("invalid sequence"),
		},
		"successful notification": {
			event: eventRankExit,
			expResp: &mgmtpb.ClusterEventResp{
				Sequence: 1,
			},
			expDispatched: []events.Event{eventRankExit},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mgmtSvc := newTestMgmtSvc(t, log)

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			mgmtSvc.events = ps

			dispatched := &eventsDispatched{cancel: cancel}
			mgmtSvc.events.Subscribe(events.RASTypeRankStateChange, dispatched)

			var pbReq *mgmtpb.ClusterEventReq
			switch {
			case tc.nilReq:
			case tc.zeroSeq:
				pbReq = &mgmtpb.ClusterEventReq{Sequence: 0}
			default:
				eventPB, err := tc.event.ToProto()
				if err != nil {
					t.Fatal(err)
				}

				pbReq = &mgmtpb.ClusterEventReq{
					Sequence: 1,
					Event: &mgmtpb.ClusterEventReq_Ras{
						Ras: eventPB,
					},
				}
			}

			gotResp, gotErr := mgmtSvc.ClusterEvent(context.TODO(), pbReq)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			<-ctx.Done()

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			common.AssertEqual(t, tc.expDispatched, dispatched.rx,
				"unexpected events dispatched")
		})
	}
}

func TestServer_MgmtSvc_getPeerListenAddr(t *testing.T) {
	defaultAddr, err := net.ResolveTCPAddr("tcp", "127.0.0.1:10001")
	if err != nil {
		t.Fatal(err)
	}
	ipAddr, err := net.ResolveIPAddr("ip", "localhost")
	if err != nil {
		t.Fatal(err)
	}
	combinedAddr, err := net.ResolveTCPAddr("tcp", "127.0.0.1:15001")
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		ctx     context.Context
		addr    string
		expAddr net.Addr
		expErr  error
	}{
		"no peer": {
			ctx:    context.Background(),
			expErr: errors.New("peer details not found in context"),
		},
		"bad input address": {
			ctx:    peer.NewContext(context.Background(), &peer.Peer{Addr: defaultAddr}),
			expErr: errors.New("get listening port: missing port in address"),
		},
		"non tcp address": {
			ctx:    peer.NewContext(context.Background(), &peer.Peer{Addr: ipAddr}),
			expErr: errors.New("peer address (127.0.0.1) not tcp"),
		},
		"normal operation": {
			ctx:     peer.NewContext(context.Background(), &peer.Peer{Addr: defaultAddr}),
			addr:    "0.0.0.0:15001",
			expAddr: combinedAddr,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotAddr, gotErr := getPeerListenAddr(tc.ctx, tc.addr)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expAddr, gotAddr); diff != "" {
				t.Fatalf("unexpected address (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func mockMember(t *testing.T, r, a int32, s string) *system.Member {
	t.Helper()

	state := map[string]system.MemberState{
		"awaitformat":  system.MemberStateAwaitFormat,
		"errored":      system.MemberStateErrored,
		"evicted":      system.MemberStateEvicted,
		"joined":       system.MemberStateJoined,
		"ready":        system.MemberStateReady,
		"starting":     system.MemberStateStarting,
		"stopped":      system.MemberStateStopped,
		"stopping":     system.MemberStateStopping,
		"unknown":      system.MemberStateUnknown,
		"unresponsive": system.MemberStateUnresponsive,
	}[s]

	if state == system.MemberStateUnknown && s != "unknown" {
		t.Fatalf("testcase specifies unknown member state %s", s)
	}

	return system.NewMember(system.Rank(r), common.MockUUID(r), "", common.MockHostAddr(a), state)
}

func mgmtSystemTestSetup(t *testing.T, l logging.Logger, mbs system.Members, r []*control.HostResponse) *mgmtSvc {
	t.Helper()

	mockResolver := func(_ string, addr string) (*net.TCPAddr, error) {
		return map[string]*net.TCPAddr{
				"10.0.0.1:10001": {IP: net.ParseIP("10.0.0.1"), Port: 10001},
				"10.0.0.2:10001": {IP: net.ParseIP("10.0.0.2"), Port: 10001},
				"10.0.0.3:10001": {IP: net.ParseIP("10.0.0.3"), Port: 10001},
				"10.0.0.4:10001": {IP: net.ParseIP("10.0.0.4"), Port: 10001},
			}[addr], map[string]error{
				"10.0.0.5:10001": errors.New("bad lookup"),
			}[addr]
	}

	mgmtSvc := newTestMgmtSvcMulti(t, l, maxIOServers, false)
	mgmtSvc.harness.started.SetTrue()
	mgmtSvc.harness.instances[0]._superblock.Rank = system.NewRankPtr(0)
	mgmtSvc.membership, _ = system.MockMembership(t, l, mockResolver)
	for _, m := range mbs {
		if _, err := mgmtSvc.membership.Add(m); err != nil {
			t.Fatal(err)
		}
	}

	mi := control.NewMockInvoker(l, &control.MockInvokerConfig{
		UnaryResponse: &control.UnaryResponse{
			Responses: r,
		},
	})
	mgmtSvc.rpcClient = mi

	return mgmtSvc
}

func TestServer_MgmtSvc_rpcFanout(t *testing.T) {
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
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 2, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 1, "joined"),
				mockMember(t, 4, 3, "joined"),
				mockMember(t, 5, 3, "joined"),
				mockMember(t, 6, 4, "joined"),
				mockMember(t, 7, 4, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "errored").WithInfo("fatality"),
				mockMember(t, 1, 2, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 1, "joined"),
				mockMember(t, 4, 3, "unresponsive").WithInfo("connection refused"),
				mockMember(t, 5, 3, "unresponsive").WithInfo("connection refused"),
				mockMember(t, 6, 4, "unresponsive").WithInfo("connection refused"),
				mockMember(t, 7, 4, "unresponsive").WithInfo("connection refused"),
			},
			expRanks: "0-7",
		},
		"filtered and oversubscribed ranks": {
			fanReq: fanoutRequest{Method: control.PingRanks, Ranks: "0-3,6-10"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 2, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 1, "joined"),
				mockMember(t, 4, 3, "joined"),
				mockMember(t, 5, 3, "joined"),
				mockMember(t, 6, 4, "joined"),
				mockMember(t, 7, 4, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "errored").WithInfo("fatality"),
				mockMember(t, 1, 2, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 1, "joined"),
				mockMember(t, 4, 3, "joined"),
				mockMember(t, 5, 3, "joined"),
				mockMember(t, 6, 4, "unresponsive").WithInfo("connection refused"),
				mockMember(t, 7, 4, "unresponsive").WithInfo("connection refused"),
			},
			expRanks:       "0-3,6-7",
			expAbsentRanks: "8-10",
		},
		"filtered and oversubscribed hosts": {
			fanReq: fanoutRequest{Method: control.PingRanks, Hosts: "10.0.0.[1-3,5]"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 2, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 1, "joined"),
				mockMember(t, 4, 3, "joined"),
				mockMember(t, 5, 3, "joined"),
				mockMember(t, 6, 4, "joined"),
				mockMember(t, 7, 4, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "errored").WithInfo("fatality"),
				mockMember(t, 1, 2, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 1, "joined"),
				mockMember(t, 4, 3, "unresponsive").WithInfo("connection refused"),
				mockMember(t, 5, 3, "unresponsive").WithInfo("connection refused"),
				mockMember(t, 6, 4, "joined"),
				mockMember(t, 7, 4, "joined"),
			},
			expRanks:       "0-5",
			expAbsentHosts: "10.0.0.5",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cs := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			var expErr error
			if tc.expErrMsg != "" {
				expErr = errors.New(tc.expErrMsg)
			}
			gotResp, gotRankSet, gotErr := cs.rpcFanout(context.TODO(), tc.fanReq, true)
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

func TestServer_MgmtSvc_SystemQuery(t *testing.T) {
	defaultMembers := system.Members{
		mockMember(t, 0, 1, "errored").WithInfo("couldn't ping"),
		mockMember(t, 1, 1, "stopping"),
		mockMember(t, 2, 2, "unresponsive"),
		mockMember(t, 3, 2, "joined"),
		mockMember(t, 4, 3, "starting"),
		mockMember(t, 5, 3, "stopped"),
	}

	for name, tc := range map[string]struct {
		nilReq         bool
		ranks          string
		hosts          string
		expMembers     []*mgmtpb.SystemMember
		expRanks       string
		expAbsentHosts string
		expAbsentRanks string
		expErrMsg      string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil request",
		},
		"unfiltered rank results": {
			expMembers: []*mgmtpb.SystemMember{
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
			expMembers: []*mgmtpb.SystemMember{
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
			hosts: "10.0.0.[2-5]",
			expMembers: []*mgmtpb.SystemMember{
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
			expRanks:       "2-5",
			expAbsentHosts: "10.0.0.[4-5]",
		},
		"missing hosts": {
			hosts:          "10.0.0.[4-5]",
			expRanks:       "",
			expAbsentHosts: "10.0.0.[4-5]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mockResolver := func(_ string, addr string) (*net.TCPAddr, error) {
				return map[string]*net.TCPAddr{
						"10.0.0.2:10001": {IP: net.ParseIP("10.0.0.2"), Port: 10001},
						"10.0.0.3:10001": {IP: net.ParseIP("10.0.0.3"), Port: 10001},
					}[addr], map[string]error{
						"10.0.0.4:10001": errors.New("bad lookup"),
						"10.0.0.5:10001": errors.New("bad lookup"),
					}[addr]
			}

			svc := newTestMgmtSvc(t, log)
			svc.membership = svc.membership.WithTCPResolver(mockResolver)
			//cs.srvCfg = config.DefaultServer().WithControlPort(10001)

			for _, m := range defaultMembers {
				if _, err := svc.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			req := &mgmtpb.SystemQueryReq{
				Sys:   build.DefaultSystemName,
				Ranks: tc.ranks, Hosts: tc.hosts,
			}
			if tc.nilReq {
				req = nil
			}

			gotResp, gotErr := svc.SystemQuery(context.TODO(), req)
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

func TestServer_MgmtSvc_SystemStart(t *testing.T) {
	for name, tc := range map[string]struct {
		nilReq         bool
		ranks          string
		hosts          string
		members        system.Members
		mResps         []*control.HostResponse
		expMembers     system.Members
		expResults     []*sharedpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expErrMsg      string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil request",
		},
		"unfiltered rank results": {
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "ready"),
				mockMember(t, 2, 2, "ready"),
				mockMember(t, 3, 2, "ready"),
			},
		},
		"filtered and oversubscribed ranks": {
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			ranks: "0-1,4-9",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			expAbsentRanks: "4-9",
		},
		"filtered and oversubscribed hosts": {
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			hosts: "10.0.0.[2-5]",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "ready"),
			},
			expAbsentHosts: "10.0.0.[3-5]",
		},
		"filtered hosts": {
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "joined"),
			},
			hosts: "10.0.0.[1-2]",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "ready"),
				mockMember(t, 3, 2, "joined"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cs := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			req := &mgmtpb.SystemStartReq{
				Sys:   build.DefaultSystemName,
				Ranks: tc.ranks, Hosts: tc.hosts,
			}
			if tc.nilReq {
				req = nil
			}

			gotResp, gotErr := cs.SystemStart(context.TODO(), req)
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

func TestServer_MgmtSvc_SystemStop(t *testing.T) {
	for name, tc := range map[string]struct {
		req            *mgmtpb.SystemStopReq
		members        system.Members
		mResps         []*control.HostResponse
		expMembers     system.Members
		expResults     []*sharedpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expErrMsg      string
	}{
		"nil req": {
			req:       (*mgmtpb.SystemStopReq)(nil),
			expErrMsg: "nil request",
		},
		"invalid req": {
			req:       new(mgmtpb.SystemStopReq),
			expErrMsg: "response results not populated",
		},
		"unfiltered prep fail": {
			req: &mgmtpb.SystemStopReq{Prep: true, Kill: true},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "stopping"),
			},
		},
		"filtered and oversubscribed ranks prep fail": {
			req: &mgmtpb.SystemStopReq{Prep: true, Kill: true, Ranks: "0-1,9"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "stopping"),
				mockMember(t, 3, 2, "joined"),
			},
			expAbsentRanks: "9",
		},
		"filtered and oversubscribed hosts prep fail": {
			req: &mgmtpb.SystemStopReq{Prep: true, Kill: true, Hosts: "10.0.0.[1,3]"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "stopping"),
				mockMember(t, 3, 2, "joined"),
			},
			expAbsentHosts: "10.0.0.3",
		},
		"unfiltered rank results": {
			req: &mgmtpb.SystemStopReq{Prep: false, Kill: true},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
		},
		"filtered and oversubscribed ranks": {
			req: &mgmtpb.SystemStopReq{Prep: false, Kill: true, Ranks: "0,2,3-9"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
							{
								Rank: 0, Errored: true, Msg: "couldn't stop",
								State: uint32(system.MemberStateJoined),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			expAbsentRanks: "4-9",
		},
		"filtered and oversubscribed hosts": {
			req: &mgmtpb.SystemStopReq{Prep: false, Kill: true, Hosts: "10.0.0.[2-5]"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			expAbsentHosts: "10.0.0.[3-5]",
		},
		"filtered hosts": {
			req: &mgmtpb.SystemStopReq{Prep: false, Kill: true, Hosts: "10.0.0.[1-2]"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cs := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			gotResp, gotErr := cs.SystemStop(context.TODO(), tc.req)
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

func TestServer_MgmtSvc_SystemResetFormat(t *testing.T) {
	for name, tc := range map[string]struct {
		nilReq         bool
		ranks          string
		hosts          string
		members        system.Members
		mResps         []*control.HostResponse
		expMembers     system.Members
		expResults     []*sharedpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expErrMsg      string
	}{
		"nil req": {
			nilReq:    true,
			expErrMsg: "nil request",
		},
		"unfiltered rank results": {
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemResetFormatResp{
						Results: []*sharedpb.RankResult{
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
					Message: &mgmtpb.SystemResetFormatResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "awaitformat"),
				mockMember(t, 2, 2, "awaitformat"),
				mockMember(t, 3, 2, "awaitformat"),
			},
		},
		"filtered and oversubscribed ranks": {
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			ranks: "0-1,4-9",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemResetFormatResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "awaitformat"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			expAbsentRanks: "4-9",
		},
		"filtered and oversubscribed hosts": {
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			hosts: "10.0.0.[2-5]",
			mResps: []*control.HostResponse{
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemResetFormatResp{
						Results: []*sharedpb.RankResult{
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
			expResults: []*sharedpb.RankResult{
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
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "awaitformat"),
			},
			expAbsentHosts: "10.0.0.[3-5]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cs := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			req := &mgmtpb.SystemResetFormatReq{
				Sys:   build.DefaultSystemName,
				Ranks: tc.ranks, Hosts: tc.hosts,
			}
			if tc.nilReq {
				req = nil
			}

			gotResp, gotErr := cs.SystemResetFormat(context.TODO(), req)
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
