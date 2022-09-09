//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"net"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/grpc/peer"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
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
		rr.Addr = test.MockHostAddr(n[0]).String()
	}
	return rr
}

func mockRankSuccess(a string, r uint32, n ...int32) *sharedpb.RankResult {
	rr := &sharedpb.RankResult{Rank: r, Action: a}
	rr.State = act2state(a)
	if len(n) > 0 {
		rr.Addr = test.MockHostAddr(n[0]).String()
	}
	return rr
}

var defEvtCmpOpts = append(test.DefaultCmpOpts(),
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
				CrtTimeout:      10,
				NetDevClass:     uint32(hardware.Infiniband),
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
					NetDevClass:     uint32(hardware.Infiniband),
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
		"Server uses TCP sockets + Ethernet": {
			clientNetworkHint: &mgmtpb.ClientNetHint{
				Provider:        "ofi+tcp",
				CrtCtxShareAddr: 0,
				CrtTimeout:      5,
				NetDevClass:     uint32(hardware.Ether),
			},
			req: &mgmtpb.GetAttachInfoReq{
				Sys:      build.DefaultSystemName,
				AllRanks: true,
			},
			expResp: &mgmtpb.GetAttachInfoResp{
				ClientNetHint: &mgmtpb.ClientNetHint{
					Provider:        "ofi+tcp",
					CrtCtxShareAddr: 0,
					CrtTimeout:      5,
					NetDevClass:     uint32(hardware.Ether),
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
				Provider:        "ofi+tcp",
				CrtCtxShareAddr: 0,
				CrtTimeout:      5,
				NetDevClass:     uint32(hardware.Ether),
			},
			req: &mgmtpb.GetAttachInfoReq{
				Sys:      build.DefaultSystemName,
				AllRanks: false,
			},
			expResp: &mgmtpb.GetAttachInfoResp{
				ClientNetHint: &mgmtpb.ClientNetHint{
					Provider:        "ofi+tcp",
					CrtCtxShareAddr: 0,
					CrtTimeout:      5,
					NetDevClass:     uint32(hardware.Ether),
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
			defer test.ShowBufferOnFailure(t, buf)
			harness := NewEngineHarness(log)
			sp := storage.NewProvider(log, 0, nil, nil, nil, nil)
			srv := newTestEngine(log, true, sp)

			if err := harness.AddInstance(srv); err != nil {
				t.Fatal(err)
			}
			srv.setDrpcClient(newMockDrpcClient(nil))
			harness.started.SetTrue()

			db := raft.MockDatabaseWithAddr(t, log, msReplica.Addr)
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

			cmpOpts := test.DefaultCmpOpts()
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
			defer test.ShowBufferOnFailure(t, buf)

			svc := newTestMgmtSvc(t, log)
			db, cleanup := raft.TestDatabase(t, log)
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
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := test.DefaultCmpOpts()
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
			defer test.ShowBufferOnFailure(t, buf)

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
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			<-ctx.Done()

			cmpOpts := test.DefaultCmpOpts()
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
			addr:   "0.0.0.0:1234",
			expErr: errors.New("peer details not found in context"),
		},
		"no input address": {
			ctx:    peer.NewContext(context.Background(), &peer.Peer{Addr: defaultAddr}),
			expErr: errors.New("get listening port: missing port in address"),
		},
		"non tcp address": {
			ctx:    peer.NewContext(context.Background(), &peer.Peer{Addr: ipAddr}),
			addr:   "0.0.0.0:1234",
			expErr: errors.New("peer address (127.0.0.1) not tcp"),
		},
		"normal operation": {
			ctx:     peer.NewContext(context.Background(), &peer.Peer{Addr: defaultAddr}),
			addr:    "0.0.0.0:15001",
			expAddr: combinedAddr,
		},
		"specific addr": {
			ctx:     peer.NewContext(context.Background(), &peer.Peer{Addr: defaultAddr}),
			addr:    combinedAddr.String(),
			expAddr: combinedAddr,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotAddr, gotErr := getPeerListenAddr(tc.ctx, tc.addr)
			test.CmpErr(t, tc.expErr, gotErr)
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

	addr := test.MockHostAddr(a)
	fd, err := system.NewFaultDomain(addr.String(), strconv.Itoa(int(r)))
	if err != nil {
		t.Fatal(err)
	}
	uri := fmt.Sprintf("tcp://%s", addr)

	m := system.MockMemberFullSpec(t, system.Rank(r), test.MockUUID(r), uri, addr, state)
	m.FabricContexts = uint32(r)
	m.FaultDomain = fd
	m.Incarnation = uint64(r)

	return m
}

func checkMembers(t *testing.T, exp system.Members, ms *system.Membership) {
	t.Helper()

	test.AssertEqual(t, len(exp), len(ms.Members(nil)), "unexpected number of members")
	for _, em := range exp {
		am, err := ms.Get(em.Rank)
		if err != nil {
			t.Fatal(err)
		}
		cmpOpts := append(test.DefaultCmpOpts(),
			cmpopts.EquateApproxTime(time.Second),
		)
		if diff := cmp.Diff(em, am, cmpOpts...); diff != "" {
			t.Fatalf("unexpected members (-want, +got)\n%s\n", diff)
		}
	}
}

func checkMemberResults(t *testing.T, exp, got system.MemberResults) {
	t.Helper()

	less := func(x, y *system.MemberResult) bool { return x.Rank < y.Rank }
	cmpOpts := append(test.DefaultCmpOpts(),
		cmpopts.SortSlices(less),
	)
	if diff := cmp.Diff(exp, got, cmpOpts...); diff != "" {
		t.Fatalf("unexpected member results (-want, +got)\n%s\n", diff)
	}
}

func checkRankResults(t *testing.T, exp, got []*sharedpb.RankResult) {
	t.Helper()

	less := func(x, y *sharedpb.RankResult) bool { return x.Rank < y.Rank }
	cmpOpts := append(test.DefaultCmpOpts(),
		cmpopts.SortSlices(less),
	)
	if diff := cmp.Diff(exp, got, cmpOpts...); diff != "" {
		t.Fatalf("unexpected rank results (-want, +got)\n%s\n", diff)
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
	svc.sysdb = raft.MockDatabase(t, l)
	svc.membership = system.MockMembership(t, l, svc.sysdb, mockResolver)
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
		sysReq         systemReq
		mResps         []*control.HostResponse
		hostErrors     control.HostErrorsMap
		expFanReq      *fanoutRequest
		expResults     system.MemberResults
		expRanks       string
		expMembers     system.Members
		expAbsentRanks string
		expAbsentHosts string
		expErrMsg      string
	}{
		"nil method in request": {
			expErrMsg: "nil system request",
		},
		"hosts and ranks both specified": {
			sysReq:    &mgmtpb.SystemStartReq{Hosts: "foo-[0-99]", Ranks: "0-99"},
			expErrMsg: "ranklist and hostlist cannot both be set in request",
		},
		"empty membership": {
			sysReq:     &mgmtpb.SystemStartReq{},
			expMembers: system.Members{},
			expFanReq: &fanoutRequest{
				Method:     control.StartRanks,
				FullSystem: true,
			},
		},
		"bad hosts in request": {
			sysReq:    &mgmtpb.SystemStartReq{Hosts: "123"},
			expErrMsg: "invalid hostname \"123\"",
		},
		"bad ranks in request": {
			sysReq:    &mgmtpb.SystemStartReq{Ranks: "foo"},
			expErrMsg: "unexpected alphabetic character(s)",
		},
		"unfiltered ranks": {
			sysReq: &mgmtpb.SystemStartReq{},
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
					Addr: test.MockHostAddr(1).String(),
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
					Addr: test.MockHostAddr(2).String(),
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
					Addr:  test.MockHostAddr(3).String(),
					Error: errors.New("connection refused"),
				},
				{
					Addr:  test.MockHostAddr(4).String(),
					Error: errors.New("connection refused"),
				},
			},
			expFanReq: &fanoutRequest{
				Method:     control.StartRanks,
				Ranks:      system.MustCreateRankSet("0-7"),
				FullSystem: true,
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			expResults: system.MemberResults{
				{
					Rank: 0, Errored: true, Msg: "fatality",
					Addr:  test.MockHostAddr(1).String(),
					State: system.MemberStateErrored,
				},
				{
					Rank: 3, Addr: test.MockHostAddr(1).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 1, Addr: test.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 2, Addr: test.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 4, Msg: "connection refused",
					Addr:  test.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 5, Msg: "connection refused",
					Addr:  test.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 6, Msg: "connection refused",
					Addr:  test.MockHostAddr(4).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 7, Msg: "connection refused",
					Addr:  test.MockHostAddr(4).String(),
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
			sysReq: &mgmtpb.SystemStartReq{Ranks: "0-3,6-10"},
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
					Addr: test.MockHostAddr(1).String(),
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
					Addr: test.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
							{Rank: 1, State: stateString(system.MemberStateJoined)},
							{Rank: 2, State: stateString(system.MemberStateJoined)},
						},
					},
				},
				{
					Addr:  test.MockHostAddr(4).String(),
					Error: errors.New("connection refused"),
				},
			},
			expFanReq: &fanoutRequest{
				Method: control.StartRanks,
				Ranks:  system.MustCreateRankSet("0-3,6-7"),
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			// results from ranks outside rank list absent
			expResults: system.MemberResults{
				{
					Rank: 0, Errored: true, Msg: "fatality",
					Addr:  test.MockHostAddr(1).String(),
					State: system.MemberStateErrored,
				},
				{
					Rank: 3, Addr: test.MockHostAddr(1).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 1, Addr: test.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 2, Addr: test.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 6, Msg: "connection refused",
					Addr:  test.MockHostAddr(4).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 7, Msg: "connection refused",
					Addr:  test.MockHostAddr(4).String(),
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
			sysReq: &mgmtpb.SystemStartReq{Hosts: "10.0.0.[1-3,5]"},
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
					Addr: test.MockHostAddr(1).String(),
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
					Addr: test.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStartResp{
						Results: []*sharedpb.RankResult{
							{Rank: 1, State: stateString(system.MemberStateJoined)},
							{Rank: 2, State: stateString(system.MemberStateJoined)},
						},
					},
				},
				{
					Addr:  test.MockHostAddr(3).String(),
					Error: errors.New("connection refused"),
				},
			},
			expFanReq: &fanoutRequest{
				Method: control.StartRanks,
				Ranks:  system.MustCreateRankSet("0-5"),
			},
			// results from ranks on failing hosts generated
			// results from host responses amalgamated
			// results from ranks outside rank list absent
			expResults: system.MemberResults{
				{
					Rank: 0, Errored: true, Msg: "fatality",
					Addr:  test.MockHostAddr(1).String(),
					State: system.MemberStateErrored,
				},
				{
					Rank: 3, Addr: test.MockHostAddr(1).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 1, Addr: test.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 2, Addr: test.MockHostAddr(2).String(),
					State: system.MemberStateJoined,
				},
				{
					Rank: 4, Msg: "connection refused",
					Addr:  test.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 5, Msg: "connection refused",
					Addr:  test.MockHostAddr(3).String(),
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
		// Test case relates to DAOS-10660. Ranks 0 & 3 joined via different interfaces but
		// reside on the same host so a duplicate set of stop results is received.
		"filtered ranks; duplicate rank results": {
			sysReq: &mgmtpb.SystemStopReq{Ranks: "0-3"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 3, "joined"),
				mockMember(t, 2, 3, "joined"),
				mockMember(t, 3, 2, "joined"),
				mockMember(t, 4, 4, "joined"),
				mockMember(t, 5, 4, "joined"),
				mockMember(t, 6, 5, "joined"),
				mockMember(t, 7, 5, "joined"),
			},
			mResps: []*control.HostResponse{
				{
					Addr: test.MockHostAddr(1).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
							{Rank: 0, State: stateString(system.MemberStateStopped)},
							{Rank: 3, State: stateString(system.MemberStateStopped)},
						},
					},
				},
				{
					Addr: test.MockHostAddr(2).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
							{Rank: 0, State: stateString(system.MemberStateStopped)},
							{Rank: 3, State: stateString(system.MemberStateStopped)},
						},
					},
				},
				{
					Addr: test.MockHostAddr(3).String(),
					Message: &mgmtpb.SystemStopResp{
						Results: []*sharedpb.RankResult{
							{Rank: 1, State: stateString(system.MemberStateStopped)},
							{Rank: 2, State: stateString(system.MemberStateStopped)},
						},
					},
				},
			},
			expFanReq: &fanoutRequest{
				Method: control.StopRanks,
				Ranks:  system.MustCreateRankSet("0-3"),
			},
			// Verifies de-duplication of rank results.
			expResults: system.MemberResults{
				{
					Rank: 0, Addr: test.MockHostAddr(1).String(),
					State: system.MemberStateStopped,
				},
				{
					Rank: 1, Addr: test.MockHostAddr(3).String(),
					State: system.MemberStateStopped,
				},
				{
					Rank: 2, Addr: test.MockHostAddr(3).String(),
					State: system.MemberStateStopped,
				},
				{
					Rank: 3, Addr: test.MockHostAddr(2).String(),
					State: system.MemberStateStopped,
				},
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 3, "stopped"),
				mockMember(t, 2, 3, "stopped"),
				mockMember(t, 3, 2, "stopped"),
				mockMember(t, 4, 4, "joined"),
				mockMember(t, 5, 4, "joined"),
				mockMember(t, 6, 5, "joined"),
				mockMember(t, 7, 5, "joined"),
			},
			expRanks: "0-3",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			svc := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			var expErr error
			if tc.expErrMsg != "" {
				expErr = errors.New(tc.expErrMsg)
			}

			gotFanReq, baseResp, gotErr := svc.getFanout(tc.sysReq)
			test.CmpErr(t, expErr, gotErr)
			if gotErr != nil && tc.expErrMsg != "" {
				return
			}

			switch tc.sysReq.(type) {
			case *mgmtpb.SystemStartReq:
				gotFanReq.Method = control.StartRanks
			case *mgmtpb.SystemStopReq:
				gotFanReq.Method = control.StopRanks
			default:
				gotFanReq.Method = control.PingRanks
			}

			cmpOpts := []cmp.Option{
				cmp.Comparer(func(a, b fmt.Stringer) bool {
					switch {
					case common.InterfaceIsNil(a) && common.InterfaceIsNil(b):
						return true
					case a != nil && common.InterfaceIsNil(b):
						return a.String() == ""
					case common.InterfaceIsNil(a) && b != nil:
						return b.String() == ""
					default:
						return a.String() == b.String()
					}
				}),
				cmp.Comparer(func(a, b systemRanksFunc) bool {
					return fmt.Sprintf("%p", a) == fmt.Sprintf("%p", b)
				}),
			}
			if diff := cmp.Diff(tc.expFanReq, gotFanReq, cmpOpts...); diff != "" {
				t.Fatalf("unexpected fanout request (-want, +got)\n%s\n", diff)
			}

			gotResp, gotRankSet, gotErr := svc.rpcFanout(context.TODO(), gotFanReq, baseResp, true)
			test.CmpErr(t, expErr, gotErr)
			if tc.expErrMsg != "" {
				return
			}

			checkMemberResults(t, tc.expResults, gotResp.Results)
			checkMembers(t, tc.expMembers, svc.membership)
			if diff := cmp.Diff(tc.expRanks, gotRankSet.String(), test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected ranks (-want, +got)\n%s\n", diff) // prints on err
			}
			test.AssertEqual(t, tc.expAbsentHosts, gotResp.AbsentHosts.String(), "absent hosts")
			test.AssertEqual(t, tc.expAbsentRanks, gotResp.AbsentRanks.String(), "absent ranks")
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
		emptyDb        bool
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
					Rank: 0, Addr: test.MockHostAddr(1).String(),
					Uuid:  test.MockUUID(0),
					State: stateString(system.MemberStateErrored), Info: "couldn't ping",
					FaultDomain: "/",
				},
				{
					Rank: 1, Addr: test.MockHostAddr(1).String(),
					Uuid: test.MockUUID(1),
					// transition to "ready" illegal
					State:       stateString(system.MemberStateStopping),
					FaultDomain: "/",
				},
				{
					Rank: 2, Addr: test.MockHostAddr(2).String(),
					Uuid:        test.MockUUID(2),
					State:       stateString(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: test.MockHostAddr(2).String(),
					Uuid:        test.MockUUID(3),
					State:       stateString(system.MemberStateJoined),
					FaultDomain: "/",
				},
				{
					Rank: 4, Addr: test.MockHostAddr(3).String(),
					Uuid:        test.MockUUID(4),
					State:       stateString(system.MemberStateStarting),
					FaultDomain: "/",
				},
				{
					Rank: 5, Addr: test.MockHostAddr(3).String(),
					Uuid:        test.MockUUID(5),
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
					Rank: 0, Addr: test.MockHostAddr(1).String(),
					Uuid:  test.MockUUID(0),
					State: stateString(system.MemberStateErrored), Info: "couldn't ping",
					FaultDomain: "/",
				},
				{
					Rank: 2, Addr: test.MockHostAddr(2).String(),
					Uuid:        test.MockUUID(2),
					State:       stateString(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: test.MockHostAddr(2).String(),
					Uuid:        test.MockUUID(3),
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
					Rank: 2, Addr: test.MockHostAddr(2).String(),
					Uuid:        test.MockUUID(2),
					State:       stateString(system.MemberStateUnresponsive),
					FaultDomain: "/",
				},
				{
					Rank: 3, Addr: test.MockHostAddr(2).String(),
					Uuid:        test.MockUUID(3),
					State:       stateString(system.MemberStateJoined),
					FaultDomain: "/",
				},
				{
					Rank: 4, Addr: test.MockHostAddr(3).String(),
					Uuid:        test.MockUUID(4),
					State:       stateString(system.MemberStateStarting),
					FaultDomain: "/",
				},
				{
					Rank: 5, Addr: test.MockHostAddr(3).String(),
					Uuid:        test.MockUUID(5),
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
		"empty membership": {
			emptyDb:   true,
			expErrMsg: system.ErrRaftUnavail.Error(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

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

			if !tc.emptyDb {
				for _, m := range defaultMembers {
					if _, err := svc.membership.Add(m); err != nil {
						t.Fatal(err)
					}
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
			test.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			cmpOpts := append(test.DefaultCmpOpts(),
				protocmp.IgnoreFields(&mgmtpb.SystemMember{}, "last_update"),
			)
			if diff := cmp.Diff(tc.expMembers, gotResp.Members, cmpOpts...); diff != "" {
				t.Logf("unexpected results (-want, +got)\n%s\n", diff) // prints on err
			}
			test.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			test.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")
		})
	}
}

func TestServer_MgmtSvc_SystemStart(t *testing.T) {
	hr := func(a int32, rrs ...*sharedpb.RankResult) *control.HostResponse {
		return &control.HostResponse{
			Addr:    test.MockHostAddr(a).String(),
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
			defer test.ShowBufferOnFailure(t, buf)

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
			test.CmpErr(t, tc.expAPIErr, gotAPIErr)
			if tc.expAPIErr != nil {
				return
			}

			checkRankResults(t, tc.expResults, gotResp.Results)
			checkMembers(t, tc.expMembers, svc.membership)
			test.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			test.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")

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
			Addr:    test.MockHostAddr(a).String(),
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
	hostRespSuccess := [][]*control.HostResponse{hrps, hrss}
	hostRespStopSuccess := [][]*control.HostResponse{hrss}
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
		expInvokeCount int
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
		"prep fail": {
			req:            &mgmtpb.SystemStopReq{},
			members:        defaultMembers,
			mResps:         hostRespFail,
			expResults:     rankResPrepFail,
			expMembers:     expMembersPrepFail,
			expDispatched:  expEventsPrepFail,
			expInvokeCount: 1,
		},
		"prep success stop fail": {
			req:            &mgmtpb.SystemStopReq{},
			members:        defaultMembers,
			mResps:         hostRespStopFail,
			expResults:     rankResStopFail,
			expMembers:     expMembersStopFail,
			expDispatched:  expEventsStopFail,
			expInvokeCount: 2,
		},
		"stop some ranks": {
			req:        &mgmtpb.SystemStopReq{Ranks: "0,1"},
			members:    defaultMembers,
			mResps:     [][]*control.HostResponse{{hr(1, mockRankSuccess("stop", 0), mockRankSuccess("stop", 1))}},
			expResults: []*sharedpb.RankResult{mockRankSuccess("stop", 0, 1), mockRankSuccess("stop", 1, 1)},
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 3, 2, "joined"),
			},
			expInvokeCount: 1, // prep should not be called
		},
		"stop with all ranks (same as full system stop)": {
			req:        &mgmtpb.SystemStopReq{Ranks: "0,1,3"},
			members:    defaultMembers,
			mResps:     hostRespSuccess,
			expResults: rankResStopSuccess,
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			expInvokeCount: 2, // prep should be called
		},
		"full system stop": {
			req:        &mgmtpb.SystemStopReq{},
			members:    defaultMembers,
			mResps:     hostRespSuccess,
			expResults: rankResStopSuccess,
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			expInvokeCount: 2, // prep should be called
		},
		"full system stop (forced)": {
			req:        &mgmtpb.SystemStopReq{Force: true},
			members:    defaultMembers,
			mResps:     hostRespStopSuccess,
			expResults: rankResStopSuccess,
			expMembers: system.Members{
				mockMember(t, 0, 1, "stopped"),
				mockMember(t, 1, 1, "stopped"),
				mockMember(t, 3, 2, "stopped"),
			},
			expInvokeCount: 1, // prep should not be called
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

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
			test.CmpErr(t, tc.expAPIErr, gotAPIErr)
			if tc.expAPIErr != nil {
				return
			}

			checkRankResults(t, tc.expResults, gotResp.Results)
			checkMembers(t, tc.expMembers, svc.membership)
			test.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			test.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")

			<-ctx.Done()

			if diff := cmp.Diff(tc.expDispatched, dispatched.rx, defEvtCmpOpts...); diff != "" {
				t.Fatalf("unexpected events dispatched (-want, +got)\n%s\n", diff)
			}

			mi := svc.rpcClient.(*control.MockInvoker)
			test.AssertEqual(t, tc.expInvokeCount, mi.GetInvokeCount(), "rpc client invoke count")
		})
	}
}

func TestServer_MgmtSvc_SystemErase(t *testing.T) {
	hr := func(a int32, rrs ...*sharedpb.RankResult) *control.HostResponse {
		return &control.HostResponse{
			Addr:    test.MockHostAddr(a).String(),
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
			defer test.ShowBufferOnFailure(t, buf)

			svc := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			req := &mgmtpb.SystemEraseReq{
				Sys: build.DefaultSystemName,
			}
			if tc.nilReq {
				req = nil
			}

			gotResp, gotErr := svc.SystemErase(context.TODO(), req)
			test.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			checkRankResults(t, tc.expResults, gotResp.Results)
			checkMembers(t, tc.expMembers, svc.membership)
		})
	}
}

func TestServer_MgmtSvc_Join(t *testing.T) {
	curMember := mockMember(t, 0, 0, "excluded")
	newMember := mockMember(t, 1, 1, "joined")

	for name, tc := range map[string]struct {
		req      *mgmtpb.JoinReq
		guResp   *mgmtpb.GroupUpdateResp
		expGuReq *mgmtpb.GroupUpdateReq
		expResp  *mgmtpb.JoinResp
		expErr   error
	}{
		"bad sys": {
			req: &mgmtpb.JoinReq{
				Sys: "bad sys",
			},
			expErr: errors.New("bad sys"),
		},
		"bad uuid": {
			req: &mgmtpb.JoinReq{
				Uuid: "bad uuid",
			},
			expErr: errors.New("bad uuid"),
		},
		"bad fault domain": {
			req: &mgmtpb.JoinReq{
				SrvFaultDomain: "bad fault domain",
			},
			expErr: errors.New("bad fault domain"),
		},
		"dupe host same rank diff uuid": {
			req: &mgmtpb.JoinReq{
				Rank: curMember.Rank.Uint32(),
				Uuid: test.MockUUID(5),
			},
			expErr: errors.New("uuid changed"),
		},
		"dupe host diff rank same uuid": {
			req: &mgmtpb.JoinReq{
				Rank: 22,
				Uuid: curMember.UUID.String(),
			},
			expErr: errors.New("already exists"),
		},
		"rejoining host": {
			req: &mgmtpb.JoinReq{
				Rank:        curMember.Rank.Uint32(),
				Uuid:        curMember.UUID.String(),
				Incarnation: curMember.Incarnation + 1,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 2,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					{
						Rank:        curMember.Rank.Uint32(),
						Uri:         curMember.FabricURI,
						Incarnation: curMember.Incarnation + 1,
					},
				},
			},
			expResp: &mgmtpb.JoinResp{
				Status: 0,
				Rank:   curMember.Rank.Uint32(),
				State:  mgmtpb.JoinResp_IN,
			},
		},
		"rejoining host; NilRank": {
			req: &mgmtpb.JoinReq{
				Rank:        uint32(system.NilRank),
				Uuid:        curMember.UUID.String(),
				Incarnation: curMember.Incarnation + 1,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 2,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					{
						Rank:        curMember.Rank.Uint32(),
						Uri:         curMember.FabricURI,
						Incarnation: curMember.Incarnation + 1,
					},
				},
			},
			expResp: &mgmtpb.JoinResp{
				Status: 0,
				Rank:   curMember.Rank.Uint32(),
				State:  mgmtpb.JoinResp_IN,
			},
		},
		"new host (non local)": {
			req: &mgmtpb.JoinReq{
				Rank:        uint32(system.NilRank),
				Incarnation: newMember.Incarnation,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 2,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					// rank 0 is excluded, so shouldn't be in the map
					{
						Rank:        newMember.Rank.Uint32(),
						Uri:         newMember.FabricURI,
						Incarnation: newMember.Incarnation,
					},
				},
			},
			expResp: &mgmtpb.JoinResp{
				Status:    0,
				Rank:      newMember.Rank.Uint32(),
				State:     mgmtpb.JoinResp_IN,
				LocalJoin: false,
			},
		},
		"new host (local)": {
			req: &mgmtpb.JoinReq{
				Addr:        common.LocalhostCtrlAddr().String(),
				Uri:         "tcp://" + common.LocalhostCtrlAddr().String(),
				Rank:        uint32(system.NilRank),
				Incarnation: newMember.Incarnation,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 2,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					// rank 0 is excluded, so shouldn't be in the map
					{
						Rank:        newMember.Rank.Uint32(),
						Uri:         "tcp://" + common.LocalhostCtrlAddr().String(),
						Incarnation: newMember.Incarnation,
					},
				},
			},
			expResp: &mgmtpb.JoinResp{
				Status:    0,
				Rank:      newMember.Rank.Uint32(),
				State:     mgmtpb.JoinResp_IN,
				LocalJoin: true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			// Make a copy to avoid test side-effects.
			curCopy := &system.Member{}
			*curCopy = *curMember
			curCopy.Rank = system.NilRank // ensure that db.data.NextRank is incremented

			svc := mgmtSystemTestSetup(t, log, system.Members{curCopy}, nil)

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			svc.startJoinLoop(ctx)

			if tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			if tc.req.Uuid == "" {
				tc.req.Uuid = newMember.UUID.String()
			}
			if tc.req.Addr == "" {
				tc.req.Addr = newMember.Addr.String()
			}
			if tc.req.Uri == "" {
				tc.req.Uri = newMember.FabricURI
			}
			if tc.req.SrvFaultDomain == "" {
				tc.req.SrvFaultDomain = newMember.FaultDomain.String()
			}
			if tc.req.Nctxs == 0 {
				tc.req.Nctxs = newMember.FabricContexts
			}
			if tc.req.Incarnation == 0 {
				tc.req.Incarnation = newMember.Incarnation
			}
			peerAddr, err := net.ResolveTCPAddr("tcp", tc.req.Addr)
			if err != nil {
				t.Fatal(err)
			}
			peerCtx := peer.NewContext(ctx, &peer.Peer{Addr: peerAddr})

			setupMockDrpcClient(svc, tc.guResp, nil)
			ei := svc.harness.instances[0].(*EngineInstance)
			mdc := ei._drpcClient.(*mockDrpcClient)

			gotResp, gotErr := svc.Join(peerCtx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotGuReq := new(mgmtpb.GroupUpdateReq)
			if err := proto.Unmarshal(mdc.calls[len(mdc.calls)-1].Body, gotGuReq); err != nil {
				t.Fatal(err)
			}
			cmpOpts := cmp.Options{
				protocmp.Transform(),
				protocmp.SortRepeatedFields(&mgmtpb.GroupUpdateReq{}, "engines"),
			}
			if diff := cmp.Diff(tc.expGuReq, gotGuReq, cmpOpts...); diff != "" {
				t.Fatalf("unexpected GroupUpdate request (-want, +got):\n%s", diff)
			}

			if diff := cmp.Diff(tc.expResp, gotResp, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
