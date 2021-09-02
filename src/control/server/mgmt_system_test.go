//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"net"
	"sort"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/grpc/peer"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func act2state(a string) string {
	switch a {
	case "prep shutdown":
		return stateString(system.MemberStateStopping)
	case "stop":
		return stateString(system.MemberStateStopped)
	case "start":
		return stateString(system.MemberStateReady)
	case "reset format":
		return stateString(system.MemberStateAwaitFormat)
	default:
		return ""
	}
}

func mockRankFail(a string, r uint32, n ...int32) *sharedpb.RankResult {
	rr := &sharedpb.RankResult{
		Rank: r, Errored: true, Msg: a + " failed",
		State:  stateString(system.MemberStateErrored),
		Action: a,
	}
	if len(n) > 0 {
		rr.Addr = common.MockHostAddr(n[0]).String()
	}
	return rr
}

func mockRankSuccess(a string, r uint32, n ...int32) *sharedpb.RankResult {
	rr := &sharedpb.RankResult{Rank: r, Action: a}
	rr.State = act2state(a)
	if len(n) > 0 {
		rr.Addr = common.MockHostAddr(n[0]).String()
	}
	return rr
}

var defEvtCmpOpts = append(common.DefaultCmpOpts(),
	cmpopts.IgnoreUnexported(events.RASEvent{}),
	cmpopts.IgnoreFields(events.RASEvent{}, "Timestamp"))

func TestServer_MgmtSvc_GetAttachInfo(t *testing.T) {
	msReplica := system.MockMember(t, 0, system.MemberStateJoined)
	nonReplica := system.MockMember(t, 1, system.MemberStateJoined)

	for name, tc := range map[string]struct {
		svc               *mgmtSvc
		clientNetworkHint *mgmtpb.ClientNetHint
		req               *mgmtpb.GetAttachInfoReq
		expResp           *mgmtpb.GetAttachInfoResp
	}{
		"Server uses verbs + Infiniband": {
			clientNetworkHint: &mgmtpb.ClientNetHint{
				Provider:        "ofi+verbs",
				CrtCtxShareAddr: 1,
				CrtTimeout:      10, NetDevClass: netdetect.Infiniband,
			},
			req: &mgmtpb.GetAttachInfoReq{
				Sys:      build.DefaultSystemName,
				AllRanks: true,
			},
			expResp: &mgmtpb.GetAttachInfoResp{
				ClientNetHint: &mgmtpb.ClientNetHint{
					Provider:        "ofi+verbs",
					CrtCtxShareAddr: 1,
					CrtTimeout:      10,
					NetDevClass:     netdetect.Infiniband,
				},
				RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
					{
						Rank: msReplica.Rank.Uint32(),
						Uri:  msReplica.FabricURI,
					},
					{
						Rank: nonReplica.Rank.Uint32(),
						Uri:  nonReplica.FabricURI,
					},
				},
				MsRanks: []uint32{0},
			},
		},
		"Server uses sockets + Ethernet": {
			clientNetworkHint: &mgmtpb.ClientNetHint{
				Provider:        "ofi+sockets",
				CrtCtxShareAddr: 0,
				CrtTimeout:      5,
				NetDevClass:     netdetect.Ether,
			},
			req: &mgmtpb.GetAttachInfoReq{
				Sys:      build.DefaultSystemName,
				AllRanks: true,
			},
			expResp: &mgmtpb.GetAttachInfoResp{
				ClientNetHint: &mgmtpb.ClientNetHint{
					Provider:        "ofi+sockets",
					CrtCtxShareAddr: 0,
					CrtTimeout:      5,
					NetDevClass:     netdetect.Ether,
				},
				RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
					{
						Rank: msReplica.Rank.Uint32(),
						Uri:  msReplica.FabricURI,
					},
					{
						Rank: nonReplica.Rank.Uint32(),
						Uri:  nonReplica.FabricURI,
					},
				},
				MsRanks: []uint32{0},
			},
		},
		"older client (AllRanks: false)": {
			clientNetworkHint: &mgmtpb.ClientNetHint{
				Provider:        "ofi+sockets",
				CrtCtxShareAddr: 0,
				CrtTimeout:      5,
				NetDevClass:     netdetect.Ether,
			},
			req: &mgmtpb.GetAttachInfoReq{
				Sys:      build.DefaultSystemName,
				AllRanks: false,
			},
			expResp: &mgmtpb.GetAttachInfoResp{
				ClientNetHint: &mgmtpb.ClientNetHint{
					Provider:        "ofi+sockets",
					CrtCtxShareAddr: 0,
					CrtTimeout:      5,
					NetDevClass:     netdetect.Ether,
				},
				RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
					{
						Rank: msReplica.Rank.Uint32(),
						Uri:  msReplica.FabricURI,
					},
				},
				MsRanks: []uint32{0},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)
			harness := NewEngineHarness(log)
			sp := storage.NewProvider(log, 0, nil, nil, nil, nil)
			srv := newTestEngine(log, true, sp)

			if err := harness.AddInstance(srv); err != nil {
				t.Fatal(err)
			}
			srv.setDrpcClient(newMockDrpcClient(nil))
			harness.started.SetTrue()

			db := system.MockDatabaseWithAddr(t, log, msReplica.Addr)
			m := system.NewMembership(log, db)
			tc.svc = newMgmtSvc(harness, m, db, nil, nil)
			if _, err := tc.svc.membership.Add(msReplica); err != nil {
				t.Fatal(err)
			}
			if _, err := tc.svc.membership.Add(nonReplica); err != nil {
				t.Fatal(err)
			}
			tc.svc.clientNetworkHint = tc.clientNetworkHint
			gotResp, gotErr := tc.svc.GetAttachInfo(context.TODO(), tc.req)
			if gotErr != nil {
				t.Fatalf("unexpected error: %+v\n", gotErr)
			}

			// Sort the "want" and "got" RankUris slices by rank before comparing them.
			for _, r := range [][]*mgmtpb.GetAttachInfoResp_RankUri{tc.expResp.RankUris, gotResp.RankUris} {
				sort.Slice(r, func(i, j int) bool { return r[i].Rank < r[j].Rank })
			}

			cmpOpts := common.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func stateString(s system.MemberState) string {
	return strings.ToLower(s.String())
}

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

			svc := newTestMgmtSvc(t, log)
			db, cleanup := system.TestDatabase(t, log)
			defer cleanup()
			svc.sysdb = db

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
				time.Sleep(250 * time.Millisecond)
			}

			gotResp, gotErr := svc.LeaderQuery(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := common.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

type eventsDispatched struct {
	rx     []*events.RASEvent
	cancel context.CancelFunc
}

func (d *eventsDispatched) OnEvent(ctx context.Context, e *events.RASEvent) {
	d.rx = append(d.rx, e)
	d.cancel()
}

func TestServer_MgmtSvc_ClusterEvent(t *testing.T) {
	eventEngineDied := mockEvtEngineDied(t)

	for name, tc := range map[string]struct {
		nilReq        bool
		zeroSeq       bool
		event         *events.RASEvent
		expResp       *sharedpb.ClusterEventResp
		expDispatched []*events.RASEvent
		expErr        error
	}{
		"nil request": {
			nilReq: true,
			expErr: errors.New("nil request"),
		},
		"successful notification": {
			event: eventEngineDied,
			expResp: &sharedpb.ClusterEventResp{
				Sequence: 1,
			},
			expDispatched: []*events.RASEvent{
				eventEngineDied.WithForwarded(true),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			svc := newTestMgmtSvc(t, log)

			ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			svc.events = ps

			dispatched := &eventsDispatched{cancel: cancel}
			svc.events.Subscribe(events.RASTypeStateChange, dispatched)

			var pbReq *sharedpb.ClusterEventReq
			switch {
			case tc.nilReq:
			case tc.zeroSeq:
				pbReq = &sharedpb.ClusterEventReq{Sequence: 0}
			default:
				eventPB, err := tc.event.ToProto()
				if err != nil {
					t.Fatal(err)
				}

				pbReq = &sharedpb.ClusterEventReq{
					Sequence: 1,
					Event:    eventPB,
				}
			}

			gotResp, gotErr := svc.ClusterEvent(context.TODO(), pbReq)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			<-ctx.Done()

			cmpOpts := common.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			if diff := cmp.Diff(tc.expDispatched, dispatched.rx, defEvtCmpOpts...); diff != "" {
				t.Fatalf("unexpected events dispatched (-want, +got)\n%s\n", diff)
			}
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
		"excluded":     system.MemberStateExcluded,
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

func checkMembers(t *testing.T, exp system.Members, ms *system.Membership) {
	t.Helper()

	common.AssertEqual(t, len(exp), len(ms.Members(nil)),
		"unexpected number of members")
	for _, em := range exp {
		am, err := ms.Get(em.Rank)
		if err != nil {
			t.Fatal(err)
		}

		// state is not exported so compare using access method
		if diff := cmp.Diff(em.State(), am.State()); diff != "" {
			t.Fatalf("unexpected member state for rank %d (-want, +got)\n%s\n", em.Rank, diff)
		}

		cmpOpts := []cmp.Option{
			cmpopts.IgnoreUnexported(system.Member{}),
			cmpopts.EquateApproxTime(time.Second),
		}
		if diff := cmp.Diff(em, am, cmpOpts...); diff != "" {
			t.Fatalf("unexpected members (-want, +got)\n%s\n", diff)
		}
	}
}

// mgmtSystemTestSetup configures a mock mgmt service and if multiple slices of
// host responses are provided then UnaryResponseSet will be populated in mock
// invoker.
func mgmtSystemTestSetup(t *testing.T, l logging.Logger, mbs system.Members, r ...[]*control.HostResponse) *mgmtSvc {
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

	svc := newTestMgmtSvcMulti(t, l, maxEngines, false)
	svc.harness.started.SetTrue()
	svc.harness.instances[0].(*EngineInstance)._superblock.Rank = system.NewRankPtr(0)
	svc.membership, _ = system.MockMembership(t, l, mockResolver)
	for _, m := range mbs {
		if _, err := svc.membership.Add(m); err != nil {
			t.Fatal(err)
		}
	}

	mic := control.MockInvokerConfig{}
	switch len(r) {
	case 0:
		t.Fatal("no host responses provided")
	case 1:
		mic.UnaryResponse = &control.UnaryResponse{Responses: r[0]}
	default:
		// multiple host response slices provided so iterate through
		// slices in successive invocations
		for i := range r {
			mic.UnaryResponseSet = append(mic.UnaryResponseSet,
				&control.UnaryResponse{Responses: r[i]})
		}
	}
	mi := control.NewMockInvoker(l, &mic)
	svc.rpcClient = mi

	return svc
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
								State: stateString(system.MemberStateErrored),
							},
							{Rank: 3, State: stateString(system.MemberStateJoined)},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
							{
								Rank:  1,
								State: stateString(system.MemberStateJoined),
							},
							{
								Rank:  2,
								State: stateString(system.MemberStateJoined),
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
								State: stateString(system.MemberStateErrored),
							},
							{
								Rank:  3,
								State: stateString(system.MemberStateJoined),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
							{Rank: 1, State: stateString(system.MemberStateJoined)},
							{Rank: 2, State: stateString(system.MemberStateJoined)},
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
								State: stateString(system.MemberStateErrored),
							},
							{
								Rank:  3,
								State: stateString(system.MemberStateJoined),
							},
						},
					},
				},
				{
					Addr: common.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
							{Rank: 1, State: stateString(system.MemberStateJoined)},
							{Rank: 2, State: stateString(system.MemberStateJoined)},
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

			svc := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			var expErr error
			if tc.expErrMsg != "" {
				expErr = errors.New(tc.expErrMsg)
			}
			gotResp, gotRankSet, gotErr := svc.rpcFanout(context.TODO(), tc.fanReq, true)
			common.CmpErr(t, expErr, gotErr)
			if tc.expErrMsg != "" {
				return
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(system.MemberResult{}, system.Member{}),
				cmpopts.EquateApproxTime(time.Second),
			}
			if diff := cmp.Diff(tc.expResults, gotResp.Results, cmpOpts...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResp.Results, name)
			checkMembers(t, tc.expMembers, svc.membership)
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
					State: stateString(system.MemberStateErrored), Info: "couldn't ping",
					FaultDomain: "/",
				},
				{
					Rank: 1, Addr: common.MockHostAddr(1).String(),
					Uuid: common.MockUUID(1),
					// transition to "ready" illegal
					State:       stateString(system.MemberStateStopping),
					FaultDomain: "/",
				},
				{
					Rank: 2, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(2),
					State:       stateString(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(3),
					State:       stateString(system.MemberStateJoined),
					FaultDomain: "/",
				},
				{
					Rank: 4, Addr: common.MockHostAddr(3).String(),
					Uuid:        common.MockUUID(4),
					State:       stateString(system.MemberStateStarting),
					FaultDomain: "/",
				},
				{
					Rank: 5, Addr: common.MockHostAddr(3).String(),
					Uuid:        common.MockUUID(5),
					State:       stateString(system.MemberStateStopped),
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
					State: stateString(system.MemberStateErrored), Info: "couldn't ping",
					FaultDomain: "/",
				},
				{
					Rank: 2, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(2),
					State:       stateString(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(3),
					State:       stateString(system.MemberStateJoined),
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
					State:       stateString(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: common.MockHostAddr(2).String(),
					Uuid:        common.MockUUID(3),
					State:       stateString(system.MemberStateJoined),
					FaultDomain: "/",
				},
				{
					Rank: 4, Addr: common.MockHostAddr(3).String(),
					Uuid:        common.MockUUID(4),
					State:       stateString(system.MemberStateStarting),
					FaultDomain: "/",
				},
				{
					Rank: 5, Addr: common.MockHostAddr(3).String(),
					Uuid:        common.MockUUID(5),
					State:       stateString(system.MemberStateStopped),
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

			ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			defer ps.Close()
			svc.events = ps

			dispatched := &eventsDispatched{cancel: cancel}
			svc.events.Subscribe(events.RASTypeStateChange, dispatched)

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

			cmpOpts := append(common.DefaultCmpOpts(),
				protocmp.IgnoreFields(&mgmtpb.SystemMember{}, "last_update"),
			)
			if diff := cmp.Diff(tc.expMembers, gotResp.Members, cmpOpts...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			common.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")
		})
	}
}

func TestServer_MgmtSvc_SystemStart(t *testing.T) {
	hr := func(a int32, rrs ...*sharedpb.RankResult) *control.HostResponse {
		return &control.HostResponse{
			Addr:    common.MockHostAddr(a).String(),
			Message: &mgmtpb.SystemStartResp{Results: rrs},
		}
	}
	expEventsStartFail := []*events.RASEvent{
		newSystemStartFailedEvent("failed rank 0"),
	}

	for name, tc := range map[string]struct {
		req            *mgmtpb.SystemStartReq
		members        system.Members
		mResps         []*control.HostResponse
		expMembers     system.Members
		expResults     []*sharedpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expAPIErr      error
		expDispatched  []*events.RASEvent
	}{
		"nil req": {
			req:       (*mgmtpb.SystemStartReq)(nil),
			expAPIErr: errors.New("nil request"),
		},
		"not system leader": {
			req: &mgmtpb.SystemStartReq{
				Sys: "quack",
			},
			expAPIErr: FaultWrongSystem("quack", build.DefaultSystemName),
		},
		"unfiltered rank results": {
			req: &mgmtpb.SystemStartReq{},
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			mResps: []*control.HostResponse{
				hr(1, mockRankFail("start", 0), mockRankSuccess("start", 1)),
				hr(2, mockRankSuccess("start", 2), mockRankSuccess("start", 3)),
			},
			expResults: []*sharedpb.RankResult{
				mockRankFail("start", 0, 1),
				mockRankSuccess("start", 1, 1),
				mockRankSuccess("start", 2, 2),
				mockRankSuccess("start", 3, 2),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "errored").WithInfo("start failed"),
				mockMember(t, 1, 1, "ready"),
				mockMember(t, 2, 2, "ready"),
				mockMember(t, 3, 2, "ready"),
			},
			expDispatched: expEventsStartFail,
		},
		"filtered and oversubscribed ranks": {
			req: &mgmtpb.SystemStartReq{Ranks: "0-1,4-9"},
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			mResps: []*control.HostResponse{
				hr(1, mockRankFail("start", 0), mockRankSuccess("start", 1)),
			},
			expResults: []*sharedpb.RankResult{
				mockRankFail("start", 0, 1),
				mockRankSuccess("start", 1, 1),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "errored").WithInfo("start failed"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			expAbsentRanks: "4-9",
			expDispatched:  expEventsStartFail,
		},
		"filtered and oversubscribed hosts": {
			req: &mgmtpb.SystemStartReq{Hosts: "10.0.0.[2-5]"},
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			mResps: []*control.HostResponse{
				hr(2, mockRankFail("start", 2), mockRankSuccess("start", 3)),
			},
			expResults: []*sharedpb.RankResult{
				mockRankFail("start", 2, 2),
				mockRankSuccess("start", 3, 2),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "errored").WithInfo("start failed"),
				mockMember(t, 3, 2, "ready"),
			},
			expAbsentHosts: "10.0.0.[3-5]",
			expDispatched: []*events.RASEvent{
				newSystemStartFailedEvent("failed rank 2"),
			},
		},
		"filtered hosts": {
			req: &mgmtpb.SystemStartReq{Hosts: "10.0.0.[1-2]"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "joined"),
			},
			mResps: []*control.HostResponse{
				hr(1, mockRankSuccess("start", 0), mockRankSuccess("start", 1)),
				hr(2, mockRankSuccess("start", 2), mockRankSuccess("start", 3)),
			},
			expResults: []*sharedpb.RankResult{
				mockRankSuccess("start", 0, 1),
				mockRankSuccess("start", 1, 1),
				mockRankSuccess("start", 2, 2),
				mockRankSuccess("start", 3, 2),
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

			svc := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			svc.events = ps

			dispatched := &eventsDispatched{cancel: cancel}
			svc.events.Subscribe(events.RASTypeInfoOnly, dispatched)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			gotResp, gotAPIErr := svc.SystemStart(context.TODO(), tc.req)
			common.CmpErr(t, tc.expAPIErr, gotAPIErr)
			if tc.expAPIErr != nil {
				return
			}

			cmpOpts := append(common.DefaultCmpOpts(),
				protocmp.IgnoreFields(&mgmtpb.SystemMember{}, "last_update"),
			)
			if diff := cmp.Diff(tc.expResults, gotResp.Results, cmpOpts...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResp.Results, name)
			checkMembers(t, tc.expMembers, svc.membership)
			common.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			common.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")

			<-ctx.Done()

			if diff := cmp.Diff(tc.expDispatched, dispatched.rx, defEvtCmpOpts...); diff != "" {
				t.Fatalf("unexpected events dispatched (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_SystemStop(t *testing.T) {
	defaultMembers := system.Members{
		mockMember(t, 0, 1, "joined"),
		mockMember(t, 1, 1, "joined"),
		mockMember(t, 3, 2, "joined"),
	}
	emf := func(a string) system.Members {
		return system.Members{
			// updated to err on prep fail if not forced
			mockMember(t, 0, 1, "errored").WithInfo(a + " failed"),
			mockMember(t, 1, 1, act2state(a)),
			mockMember(t, 3, 2, "errored").WithInfo(a + " failed"),
		}
	}
	expMembersPrepFail := emf("prep shutdown")
	expMembersStopFail := emf("stop")
	hr := func(a int32, rrs ...*sharedpb.RankResult) *control.HostResponse {
		return &control.HostResponse{
			Addr:    common.MockHostAddr(a).String(),
			Message: &mgmtpb.SystemStopResp{Results: rrs},
		}
	}
	hrpf := []*control.HostResponse{
		hr(1, mockRankFail("prep shutdown", 0), mockRankSuccess("prep shutdown", 1)),
		hr(2, mockRankFail("prep shutdown", 3)),
	}
	hrps := []*control.HostResponse{
		hr(1, mockRankSuccess("prep shutdown", 0), mockRankSuccess("prep shutdown", 1)),
		hr(2, mockRankSuccess("prep shutdown", 3)),
	}
	hrsf := []*control.HostResponse{
		hr(1, mockRankFail("stop", 0), mockRankSuccess("stop", 1)),
		hr(2, mockRankFail("stop", 3)),
	}
	hrss := []*control.HostResponse{
		hr(1, mockRankSuccess("stop", 0), mockRankSuccess("stop", 1)),
		hr(2, mockRankSuccess("stop", 3)),
	}
	// simulates prep shutdown followed by stop dRPCs
	hostRespFail := [][]*control.HostResponse{hrpf, hrsf}
	hostRespStopFail := [][]*control.HostResponse{hrps, hrsf}
	hostRespStopSuccess := [][]*control.HostResponse{hrpf, hrss}
	hostRespSuccess := [][]*control.HostResponse{hrps, hrss}
	rankResPrepFail := []*sharedpb.RankResult{
		mockRankFail("prep shutdown", 0, 1), mockRankSuccess("prep shutdown", 1, 1), mockRankFail("prep shutdown", 3, 2),
	}
	rankResStopFail := []*sharedpb.RankResult{
		mockRankFail("stop", 0, 1), mockRankSuccess("stop", 1, 1), mockRankFail("stop", 3, 2),
	}
	rankResStopSuccess := []*sharedpb.RankResult{
		mockRankSuccess("stop", 0, 1), mockRankSuccess("stop", 1, 1), mockRankSuccess("stop", 3, 2),
	}
	expEventsPrepFail := []*events.RASEvent{
		newSystemStopFailedEvent("prep shutdown", "failed ranks 0,3"),
	}
	expEventsStopFail := []*events.RASEvent{
		newSystemStopFailedEvent("stop", "failed ranks 0,3"),
	}

	for name, tc := range map[string]struct {
		req            *mgmtpb.SystemStopReq
		members        system.Members
		mResps         [][]*control.HostResponse
		expMembers     system.Members
		expResults     []*sharedpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expAPIErr      error
		expDispatched  []*events.RASEvent
	}{
		"nil req": {
			req:       (*mgmtpb.SystemStopReq)(nil),
			expAPIErr: errors.New("nil request"),
		},
		"not system leader": {
			req: &mgmtpb.SystemStopReq{
				Sys: "quack",
			},
			expAPIErr: FaultWrongSystem("quack", build.DefaultSystemName),
		},
		"unfiltered prep fail": {
			req:           &mgmtpb.SystemStopReq{},
			members:       defaultMembers,
			mResps:        hostRespFail,
			expResults:    rankResPrepFail,
			expMembers:    expMembersPrepFail,
			expDispatched: expEventsPrepFail,
		},
		"filtered and oversubscribed ranks prep fail": {
			req:            &mgmtpb.SystemStopReq{Ranks: "0-1,9"},
			members:        defaultMembers,
			mResps:         hostRespFail,
			expResults:     rankResPrepFail,
			expMembers:     expMembersPrepFail,
			expAbsentRanks: "9",
			expDispatched:  expEventsPrepFail,
		},
		"filtered and oversubscribed hosts prep fail": {
			req:            &mgmtpb.SystemStopReq{Hosts: "10.0.0.[1-3]"},
			members:        defaultMembers,
			mResps:         hostRespFail,
			expResults:     rankResPrepFail,
			expMembers:     expMembersPrepFail,
			expAbsentHosts: "10.0.0.3",
			expDispatched:  expEventsPrepFail,
		},
		// with force set in request, prep failure will be ignored
		"prep fail with force and stop fail": {
			req:           &mgmtpb.SystemStopReq{Force: true},
			members:       defaultMembers,
			mResps:        hostRespFail,
			expResults:    rankResStopFail,
			expMembers:    expMembersStopFail,
			expDispatched: expEventsStopFail,
		},
		"prep fail with force and stop success": {
			req:        &mgmtpb.SystemStopReq{Force: true},
			members:    defaultMembers,
			mResps:     hostRespStopSuccess,
			expResults: rankResStopSuccess,
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
		},
		"prep success stop fail": {
			req:           &mgmtpb.SystemStopReq{},
			members:       defaultMembers,
			mResps:        hostRespStopFail,
			expResults:    rankResStopFail,
			expMembers:    expMembersStopFail,
			expDispatched: expEventsStopFail,
		},
		"prep success stop success": {
			req:        &mgmtpb.SystemStopReq{},
			members:    defaultMembers,
			mResps:     hostRespSuccess,
			expResults: rankResStopSuccess,
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mResps == nil {
				tc.mResps = [][]*control.HostResponse{{}}
			}
			svc := mgmtSystemTestSetup(t, log, tc.members, tc.mResps...)

			ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			svc.events = ps

			dispatched := &eventsDispatched{cancel: cancel}
			svc.events.Subscribe(events.RASTypeInfoOnly, dispatched)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			gotResp, gotAPIErr := svc.SystemStop(context.TODO(), tc.req)
			common.CmpErr(t, tc.expAPIErr, gotAPIErr)
			if tc.expAPIErr != nil {
				return
			}

			cmpOpts := append(common.DefaultCmpOpts(),
				protocmp.IgnoreFields(&mgmtpb.SystemMember{}, "last_update"),
			)
			if diff := cmp.Diff(tc.expResults, gotResp.Results, cmpOpts...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResp.Results, name)
			checkMembers(t, tc.expMembers, svc.membership)
			common.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			common.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")

			<-ctx.Done()

			if diff := cmp.Diff(tc.expDispatched, dispatched.rx, defEvtCmpOpts...); diff != "" {
				t.Fatalf("unexpected events dispatched (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_SystemErase(t *testing.T) {
	hr := func(a int32, rrs ...*sharedpb.RankResult) *control.HostResponse {
		return &control.HostResponse{
			Addr:    common.MockHostAddr(a).String(),
			Message: &mgmtpb.SystemEraseResp{Results: rrs},
		}
	}

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
				hr(1, mockRankFail("reset format", 0), mockRankSuccess("reset format", 1)),
				hr(2, mockRankSuccess("reset format", 2), mockRankSuccess("reset format", 3)),
			},
			expResults: []*sharedpb.RankResult{
				mockRankFail("reset format", 0, 1),
				mockRankSuccess("reset format", 1, 1),
				mockRankSuccess("reset format", 2, 2),
				mockRankSuccess("reset format", 3, 2),
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
			mResps: []*control.HostResponse{
				hr(1, mockRankFail("reset format", 0), mockRankSuccess("reset format", 1)),
			},
			expResults: []*sharedpb.RankResult{
				mockRankFail("reset format", 0, 1),
				mockRankSuccess("reset format", 1, 1),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "awaitformat"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
		},
		"filtered and oversubscribed hosts": {
			members: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			mResps: []*control.HostResponse{
				hr(2, mockRankFail("reset format", 2), mockRankSuccess("reset format", 3)),
			},
			expResults: []*sharedpb.RankResult{
				mockRankFail("reset format", 2, 2),
				mockRankSuccess("reset format", 3, 2),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "stopped"),
				mockMember(t, 3, 2, "awaitformat"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			svc := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			req := &mgmtpb.SystemEraseReq{
				Sys: build.DefaultSystemName,
			}
			if tc.nilReq {
				req = nil
			}

			gotResp, gotErr := svc.SystemErase(context.TODO(), req)
			common.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			cmpOpts := append(common.DefaultCmpOpts(),
				protocmp.IgnoreFields(&mgmtpb.SystemMember{}, "last_update"),
			)
			if diff := cmp.Diff(tc.expResults, gotResp.Results, cmpOpts...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			common.AssertEqual(t, tc.expResults, gotResp.Results, name)
			checkMembers(t, tc.expMembers, svc.membership)
		})
	}
}
