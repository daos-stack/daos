//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc/peer"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/mgmt"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
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
	case "set admin-excluded state":
		return stateString(system.MemberStateAdminExcluded)
	case "clear admin-excluded state":
		return stateString(system.MemberStateExcluded)
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
				Provider:    "ofi+verbs",
				CrtTimeout:  10,
				NetDevClass: uint32(hardware.Infiniband),
			},
			req: &mgmtpb.GetAttachInfoReq{
				Sys:      build.DefaultSystemName,
				AllRanks: true,
			},
			expResp: &mgmtpb.GetAttachInfoResp{
				ClientNetHint: &mgmtpb.ClientNetHint{
					Provider:    "ofi+verbs",
					CrtTimeout:  10,
					NetDevClass: uint32(hardware.Infiniband),
				},
				RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
					{
						Rank:    msReplica.Rank.Uint32(),
						Uri:     msReplica.PrimaryFabricURI,
						NumCtxs: 0,
					},
					{
						Rank:    nonReplica.Rank.Uint32(),
						Uri:     nonReplica.PrimaryFabricURI,
						NumCtxs: 1,
					},
				},
				MsRanks:     []uint32{0},
				DataVersion: 2,
				Sys:         build.DefaultSystemName,
			},
		},
		"Server uses TCP sockets + Ethernet": {
			clientNetworkHint: &mgmtpb.ClientNetHint{
				Provider:    "ofi+tcp",
				CrtTimeout:  5,
				NetDevClass: uint32(hardware.Ether),
			},
			req: &mgmtpb.GetAttachInfoReq{
				Sys:      build.DefaultSystemName,
				AllRanks: true,
			},
			expResp: &mgmtpb.GetAttachInfoResp{
				ClientNetHint: &mgmtpb.ClientNetHint{
					Provider:    "ofi+tcp",
					CrtTimeout:  5,
					NetDevClass: uint32(hardware.Ether),
				},
				RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
					{
						Rank:    msReplica.Rank.Uint32(),
						Uri:     msReplica.PrimaryFabricURI,
						NumCtxs: 0,
					},
					{
						Rank:    nonReplica.Rank.Uint32(),
						Uri:     nonReplica.PrimaryFabricURI,
						NumCtxs: 1,
					},
				},
				MsRanks:     []uint32{0},
				DataVersion: 2,
				Sys:         build.DefaultSystemName,
			},
		},
		"older client (AllRanks: false)": {
			clientNetworkHint: &mgmtpb.ClientNetHint{
				Provider:    "ofi+tcp",
				CrtTimeout:  5,
				NetDevClass: uint32(hardware.Ether),
			},
			req: &mgmtpb.GetAttachInfoReq{
				Sys:      build.DefaultSystemName,
				AllRanks: false,
			},
			expResp: &mgmtpb.GetAttachInfoResp{
				ClientNetHint: &mgmtpb.ClientNetHint{
					Provider:    "ofi+tcp",
					CrtTimeout:  5,
					NetDevClass: uint32(hardware.Ether),
				},
				RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
					{
						Rank: msReplica.Rank.Uint32(),
						Uri:  msReplica.PrimaryFabricURI,
					},
				},
				MsRanks:     []uint32{0},
				DataVersion: 2,
				Sys:         build.DefaultSystemName,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			harness := NewEngineHarness(log)
			sp := storage.NewProvider(log, 0, nil, nil, nil, nil, nil)
			srv := newTestEngine(log, true, sp)

			if err := harness.AddInstance(srv); err != nil {
				t.Fatal(err)
			}

			srv.getDrpcClientFn = func(s string) drpc.DomainSocketClient {
				return newMockDrpcClient(nil)
			}
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
			tc.svc.clientNetworkHint = []*mgmtpb.ClientNetHint{tc.clientNetworkHint}
			gotResp, gotErr := tc.svc.GetAttachInfo(test.Context(t), tc.req)
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

			ctx := test.Context(t)
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

			gotResp, gotErr := svc.LeaderQuery(test.Context(t), tc.req)
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

func TestServer_MgmtSvc_ClusterEvent(t *testing.T) {
	eventEngineDied := mockEvtEngineDied(t)

	for name, tc := range map[string]struct {
		nilReq        bool
		zeroSeq       bool
		event         *events.RASEvent
		expResp       *sharedpb.ClusterEventResp
		expDispatched []string
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
			expDispatched: []string{
				func() string {
					e := eventEngineDied.WithForwarded(true)
					e.Timestamp = ""
					return e.String()
				}(),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			svc := newTestMgmtSvc(t, log)

			ctx, cancel := context.WithTimeout(test.Context(t), 200*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			defer ps.Close()

			svc.events = ps

			subscriber := newMockSubscriber(1)
			svc.events.Subscribe(events.RASTypeStateChange, subscriber)

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

			gotResp, gotErr := svc.ClusterEvent(test.Context(t), pbReq)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			<-ctx.Done()

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			if diff := cmp.Diff(tc.expDispatched, subscriber.getRx(), defEvtCmpOpts...); diff != "" {
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
			ctx:    test.Context(t),
			addr:   "0.0.0.0:1234",
			expErr: errors.New("peer details not found in context"),
		},
		"no input address": {
			ctx:    peer.NewContext(test.Context(t), &peer.Peer{Addr: defaultAddr}),
			expErr: errors.New("get listening port: missing port in address"),
		},
		"non tcp address": {
			ctx:    peer.NewContext(test.Context(t), &peer.Peer{Addr: ipAddr}),
			addr:   "0.0.0.0:1234",
			expErr: errors.New("peer address (127.0.0.1) not tcp"),
		},
		"normal operation": {
			ctx:     peer.NewContext(test.Context(t), &peer.Peer{Addr: defaultAddr}),
			addr:    "0.0.0.0:15001",
			expAddr: combinedAddr,
		},
		"specific addr": {
			ctx:     peer.NewContext(test.Context(t), &peer.Peer{Addr: defaultAddr}),
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
		"awaitformat":   system.MemberStateAwaitFormat,
		"errored":       system.MemberStateErrored,
		"excluded":      system.MemberStateExcluded,
		"joined":        system.MemberStateJoined,
		"ready":         system.MemberStateReady,
		"starting":      system.MemberStateStarting,
		"stopped":       system.MemberStateStopped,
		"stopping":      system.MemberStateStopping,
		"unknown":       system.MemberStateUnknown,
		"unresponsive":  system.MemberStateUnresponsive,
		"adminexcluded": system.MemberStateAdminExcluded,
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

	m := system.MockMemberFullSpec(t, ranklist.Rank(r), test.MockUUID(r), uri, addr, state)
	m.PrimaryFabricContexts = uint32(r)
	m.FaultDomain = fd
	m.Incarnation = uint64(r)

	return m
}

func checkMembers(t *testing.T, exp system.Members, ms *system.Membership) {
	t.Helper()

	members, err := ms.Members(nil)
	if err != nil {
		t.Fatal(err)
	}

	test.AssertEqual(t, len(exp), len(members), "unexpected number of members")
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

func TestServer_MgmtSvc_getPoolRanks(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())

	for name, tc := range map[string]struct {
		pools        []string
		inRanks      *ranklist.RankSet
		getEnabled   bool
		drpcResps    []*mockDrpcResponse // Sequential list of dRPC responses.
		expErr       error
		expPoolRanks poolRanksMap
		expDrpcCount int
	}{
		"zero pools": {},
		"match all ranks; two pools": {
			pools:      []string{test.MockUUID(1), test.MockUUID(2)},
			getEnabled: true,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "0-4",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "1-7",
					},
				},
			},
			expPoolRanks: map[string]*ranklist.RankSet{
				test.MockUUID(1): ranklist.MustCreateRankSet("0-4"),
				test.MockUUID(2): ranklist.MustCreateRankSet("1-7"),
			},
			expDrpcCount: 2,
		},
		"match subset of ranks; two pools; get disabled ranks": {
			pools:      []string{test.MockUUID(1), test.MockUUID(2)},
			inRanks:    ranklist.MustCreateRankSet("1,8"),
			getEnabled: false,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "0-4",
						DisabledRanks: "5-8",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "1-7",
						DisabledRanks: "",
					},
				},
			},
			expPoolRanks: map[string]*ranklist.RankSet{
				test.MockUUID(1): ranklist.MustCreateRankSet("8"),
			},
			expDrpcCount: 2,
		},
		"match zero ranks; two pools": {
			pools:      []string{test.MockUUID(1), test.MockUUID(2)},
			inRanks:    ranklist.MustCreateRankSet("8-10"),
			getEnabled: true,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "0-4",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "1-7",
					},
				},
			},
			expDrpcCount: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.MustLogContext(t)
			svc := newTestMgmtSvc(t, log)

			for _, uuidStr := range tc.pools {
				addTestPoolService(t, svc.sysdb, &system.PoolService{
					PoolUUID: uuid.MustParse(uuidStr),
					State:    system.PoolServiceStateReady,
					Replicas: []ranklist.Rank{0},
				})
			}

			cfg := new(mockDrpcClientConfig)
			for _, mock := range tc.drpcResps {
				cfg.setSendMsgResponseList(t, mock)
			}
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(svc, 0, mdc)

			gotPoolIDs, gotPoolRanks, gotErr := svc.getPoolRanks(ctx, tc.inRanks,
				tc.getEnabled)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			test.AssertEqual(t, len(tc.expPoolRanks), len(gotPoolRanks),
				"len pool ranks")

			for _, id := range gotPoolIDs {
				test.AssertEqual(t, tc.expPoolRanks[id].String(),
					gotPoolRanks[id].String(), "pool ranks")
			}

			test.AssertEqual(t, tc.expDrpcCount, len(mdc.CalledMethods()),
				"rpc client invoke count")
		})
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
	svc.harness.instances[0].(*EngineInstance)._superblock.Rank = ranklist.NewRankPtr(0)
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

	if err := svc.setFabricProviders("tcp"); err != nil {
		t.Fatal(err)
	}

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
				Ranks:      ranklist.MustCreateRankSet("0-7"),
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
					Rank: 4, Msg: "connection refused", Errored: true,
					Addr:  test.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 5, Msg: "connection refused", Errored: true,
					Addr:  test.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 6, Msg: "connection refused", Errored: true,
					Addr:  test.MockHostAddr(4).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 7, Msg: "connection refused", Errored: true,
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
				Ranks:  ranklist.MustCreateRankSet("0-3,6-7"),
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
					Rank: 6, Msg: "connection refused", Errored: true,
					Addr:  test.MockHostAddr(4).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 7, Msg: "connection refused", Errored: true,
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
				Ranks:  ranklist.MustCreateRankSet("0-5"),
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
					Rank: 4, Msg: "connection refused", Errored: true,
					Addr:  test.MockHostAddr(3).String(),
					State: system.MemberStateUnresponsive,
				},
				{
					Rank: 5, Msg: "connection refused", Errored: true,
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
				Ranks:  ranklist.MustCreateRankSet("0-3"),
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
				t.Fatal("no system request specified")
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

			gotResp, gotRankSet, gotErr := svc.rpcFanout(test.Context(t), gotFanReq, baseResp, true)
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
		clientNetHints []*mgmtpb.ClientNetHint
		expMembers     []*mgmtpb.SystemMember
		expRanks       string
		expAbsentHosts string
		expAbsentRanks string
		expErrMsg      string
		expProviders   []string
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
		"use clientNetHint for providers": {
			clientNetHints: []*mgmtpb.ClientNetHint{
				{
					Provider: "prov1",
				},
				{
					Provider: "prov2",
				},
				{
					Provider: "prov3",
				},
			},
			expProviders: []string{"prov1", "prov2", "prov3"},
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
			svc.clientNetworkHint = tc.clientNetHints

			ctx, cancel := context.WithTimeout(test.Context(t), 50*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			defer ps.Close()
			svc.events = ps

			subscriber := newMockSubscriber(1)
			svc.events.Subscribe(events.RASTypeStateChange, subscriber)

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

			gotResp, gotErr := svc.SystemQuery(test.Context(t), req)
			test.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			cmpOpts := append(test.DefaultCmpOpts(),
				protocmp.IgnoreFields(&mgmtpb.SystemMember{},
					"last_update", "fault_domain", "fabric_uri", "fabric_contexts", "incarnation"),
			)
			if diff := cmp.Diff(tc.expMembers, gotResp.Members, cmpOpts...); diff != "" {
				t.Errorf("unexpected results (-want, +got)\n%s\n", diff)
			}
			test.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			test.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")
			if diff := cmp.Diff(tc.expProviders, gotResp.Providers); diff != "" {
				t.Errorf("unexpected results (-want, +got)\n%s\n", diff)
			}
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
	expEventsStartFail := func(msgErr string) []string {
		e := newSystemStartFailedEvent(msgErr)
		e.Timestamp = ""
		return []string{e.String()}
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
		expDispatched  []string
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
			expDispatched: expEventsStartFail("failed rank 0"),
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
			expDispatched:  expEventsStartFail("failed rank 0"),
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
			expDispatched:  expEventsStartFail("failed rank 2"),
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

			ctx, cancel := context.WithTimeout(test.Context(t), 200*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			svc.events = ps

			subscriber := newMockSubscriber(1)
			svc.events.Subscribe(events.RASTypeInfoOnly, subscriber)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			gotResp, gotAPIErr := svc.SystemStart(test.Context(t), tc.req)
			test.CmpErr(t, tc.expAPIErr, gotAPIErr)
			if tc.expAPIErr != nil {
				return
			}

			checkRankResults(t, tc.expResults, gotResp.Results)
			checkMembers(t, tc.expMembers, svc.membership)
			test.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			test.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")

			<-ctx.Done()

			if diff := cmp.Diff(tc.expDispatched, subscriber.getRx(), defEvtCmpOpts...); diff != "" {
				t.Fatalf("unexpected events dispatched (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_SystemStop(t *testing.T) {
	emf := func(a string) func() system.Members {
		return func() system.Members {
			return system.Members{
				// updated to err on prep fail if not forced
				mockMember(t, 0, 1, "errored").WithInfo(a + " failed"),
				mockMember(t, 1, 1, act2state(a)),
				mockMember(t, 3, 2, "errored").WithInfo(a + " failed"),
			}
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
	expEventsStopFail := func(msgErr string) []string {
		e := newSystemStopFailedEvent(msgErr, "failed ranks 0,3")
		e.Timestamp = ""
		return []string{e.String()}
	}

	for name, tc := range map[string]struct {
		req            *mgmtpb.SystemStopReq
		mResps         [][]*control.HostResponse
		expMembers     func() system.Members
		expResults     []*sharedpb.RankResult
		expAbsentRanks string
		expAbsentHosts string
		expAPIErr      error
		expDispatched  []string
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
			mResps:         hostRespFail,
			expResults:     rankResPrepFail,
			expMembers:     expMembersPrepFail,
			expDispatched:  expEventsStopFail("prep shutdown"),
			expInvokeCount: 1,
		},
		"prep success stop fail": {
			req:            &mgmtpb.SystemStopReq{},
			mResps:         hostRespStopFail,
			expResults:     rankResStopFail,
			expMembers:     expMembersStopFail,
			expDispatched:  expEventsStopFail("stop"),
			expInvokeCount: 2,
		},
		"stop some ranks": {
			req: &mgmtpb.SystemStopReq{Ranks: "0,1", Force: true},
			mResps: [][]*control.HostResponse{
				{
					hr(1, mockRankSuccess("stop", 0), mockRankSuccess("stop", 1)),
				},
			},
			expResults: []*sharedpb.RankResult{
				mockRankSuccess("stop", 0, 1),
				mockRankSuccess("stop", 1, 1),
			},
			expMembers: func() system.Members {
				return system.Members{
					mockMember(t, 0, 1, "stopped"),
					mockMember(t, 1, 1, "stopped"),
					mockMember(t, 3, 2, "joined"),
				}
			},
			expInvokeCount: 1, // prep should not be called
		},
		"stop with all ranks": {
			req:        &mgmtpb.SystemStopReq{Ranks: "0,1,3", Force: true},
			mResps:     hostRespStopSuccess,
			expResults: rankResStopSuccess,
			expMembers: func() system.Members {
				return system.Members{
					mockMember(t, 0, 1, "stopped"),
					mockMember(t, 1, 1, "stopped"),
					mockMember(t, 3, 2, "stopped"),
				}
			},
			expInvokeCount: 1, // prep should not be called
		},
		"full system stop": {
			req:        &mgmtpb.SystemStopReq{},
			mResps:     hostRespSuccess,
			expResults: rankResStopSuccess,
			expMembers: func() system.Members {
				return system.Members{
					mockMember(t, 0, 1, "stopped"),
					mockMember(t, 1, 1, "stopped"),
					mockMember(t, 3, 2, "stopped"),
				}
			},
			expInvokeCount: 2, // prep should be called
		},
		"full system stop; partial ranks in req": {
			req:       &mgmtpb.SystemStopReq{Ranks: "0,1"},
			mResps:    hostRespStopSuccess,
			expAPIErr: errSysForceNotFull,
		},
		"full system stop (forced)": {
			req:        &mgmtpb.SystemStopReq{Force: true},
			mResps:     hostRespStopSuccess,
			expResults: rankResStopSuccess,
			expMembers: func() system.Members {
				return system.Members{
					mockMember(t, 0, 1, "stopped"),
					mockMember(t, 1, 1, "stopped"),
					mockMember(t, 3, 2, "stopped"),
				}
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
			members := system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 3, 2, "joined"),
			}
			svc := mgmtSystemTestSetup(t, log, members, tc.mResps...)

			ctx, cancel := context.WithTimeout(test.Context(t), 200*time.Millisecond)
			defer cancel()

			ps := events.NewPubSub(ctx, log)
			svc.events = ps

			subscriber := newMockSubscriber(1)
			svc.events.Subscribe(events.RASTypeInfoOnly, subscriber)

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}
			gotResp, gotAPIErr := svc.SystemStop(test.Context(t), tc.req)
			test.CmpErr(t, tc.expAPIErr, gotAPIErr)
			if tc.expAPIErr != nil {
				return
			}

			checkRankResults(t, tc.expResults, gotResp.Results)
			checkMembers(t, tc.expMembers(), svc.membership)
			test.AssertEqual(t, tc.expAbsentHosts, gotResp.Absenthosts, "absent hosts")
			test.AssertEqual(t, tc.expAbsentRanks, gotResp.Absentranks, "absent ranks")

			<-ctx.Done()

			if diff := cmp.Diff(tc.expDispatched, subscriber.getRx(), defEvtCmpOpts...); diff != "" {
				t.Fatalf("unexpected events dispatched (-want, +got)\n%s\n", diff)
			}

			mi := svc.rpcClient.(*control.MockInvoker)
			test.AssertEqual(t, tc.expInvokeCount, mi.GetInvokeCount(), "rpc client invoke count")
		})
	}
}

func TestServer_MgmtSvc_SystemExclude(t *testing.T) {
	for name, tc := range map[string]struct {
		req        *mgmtpb.SystemExcludeReq
		members    system.Members
		mResps     []*control.HostResponse
		expMembers system.Members
		expResults []*sharedpb.RankResult
		expAPIErr  error
	}{
		"nil req": {
			req:       (*mgmtpb.SystemExcludeReq)(nil),
			expAPIErr: errors.New("nil request"),
		},
		"not system leader": {
			req: &mgmtpb.SystemExcludeReq{
				Sys: "quack",
			},
			expAPIErr: FaultWrongSystem("quack", build.DefaultSystemName),
		},
		"no hosts or ranks": {
			req: &mgmtpb.SystemExcludeReq{},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
			expAPIErr: errors.New("no hosts or ranks"),
		},
		"hosts and ranks": {
			req: &mgmtpb.SystemExcludeReq{
				Hosts: "host1,host2",
				Ranks: "0,1",
			},
			expAPIErr: errors.New("ranklist and hostlist"),
		},
		"invalid ranks": {
			req:       &mgmtpb.SystemExcludeReq{Ranks: "41,42"},
			expAPIErr: errors.New("invalid"),
		},
		"invalid hosts": {
			req:       &mgmtpb.SystemExcludeReq{Hosts: "host-[1-2]"},
			expAPIErr: errors.New("invalid"),
		},
		"exclude ranks": {
			req: &mgmtpb.SystemExcludeReq{Ranks: "0-1"},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
			expResults: []*sharedpb.RankResult{
				mockRankSuccess("set admin-excluded state", 0, 1),
				mockRankSuccess("set admin-excluded state", 1, 1),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "adminexcluded"),
				mockMember(t, 1, 1, "adminexcluded"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
		},
		"exclude hosts": {
			req: &mgmtpb.SystemExcludeReq{Hosts: test.MockHostAddr(1).String()},
			members: system.Members{
				mockMember(t, 0, 1, "joined"),
				mockMember(t, 1, 1, "joined"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
			expResults: []*sharedpb.RankResult{
				mockRankSuccess("set admin-excluded state", 0, 1),
				mockRankSuccess("set admin-excluded state", 1, 1),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "adminexcluded"),
				mockMember(t, 1, 1, "adminexcluded"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
		},
		"unexclude ranks": {
			req: &mgmtpb.SystemExcludeReq{Ranks: "0-1", Clear: true},
			members: system.Members{
				mockMember(t, 0, 1, "adminexcluded"),
				mockMember(t, 1, 1, "adminexcluded"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
			expResults: []*sharedpb.RankResult{
				mockRankSuccess("clear admin-excluded state", 0, 1),
				mockRankSuccess("clear admin-excluded state", 1, 1),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "excluded"),
				mockMember(t, 1, 1, "excluded"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
		},

		"unexclude hosts": {
			req: &mgmtpb.SystemExcludeReq{Hosts: test.MockHostAddr(1).String(), Clear: true},
			members: system.Members{
				mockMember(t, 0, 1, "adminexcluded"),
				mockMember(t, 1, 1, "adminexcluded"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
			expResults: []*sharedpb.RankResult{
				mockRankSuccess("clear admin-excluded state", 0, 1),
				mockRankSuccess("clear admin-excluded state", 1, 1),
			},
			expMembers: system.Members{
				mockMember(t, 0, 1, "excluded"),
				mockMember(t, 1, 1, "excluded"),
				mockMember(t, 2, 2, "joined"),
				mockMember(t, 3, 2, "joined"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			svc := mgmtSystemTestSetup(t, log, tc.members, tc.mResps)

			ctx := test.Context(t)
			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			startMapVer, err := svc.sysdb.CurMapVersion()
			if err != nil {
				t.Fatalf("startMapVer CurMapVersion() failed\n")
				return
			}
			gotResp, gotAPIErr := svc.SystemExclude(ctx, tc.req)
			test.CmpErr(t, tc.expAPIErr, gotAPIErr)
			if tc.expAPIErr != nil {
				return
			}

			// Check for any system map version increase by the (asynchronous) update.
			// Test will time out if it never happens, thus choice of an infinite loop here.
			for {
				curMapVer, err := svc.sysdb.CurMapVersion()
				if err != nil {
					t.Fatalf("CurMapVersion() failed\n")
					return
				}

				if curMapVer > startMapVer {
					break
				}
			}

			checkRankResults(t, tc.expResults, gotResp.Results)
			checkMembers(t, tc.expMembers, svc.membership)
		})
	}
}

func TestServer_MgmtSvc_SystemDrain(t *testing.T) {
	for name, tc := range map[string]struct {
		req            *mgmtpb.SystemDrainReq
		useLabels      bool
		pools          []string
		members        system.Members
		drpcResps      []*mockDrpcResponse // For dRPC PoolQuery
		expDrpcCount   int
		mic            *control.MockInvokerConfig // For control-API PoolDrain/Reint
		expCtlApiCount int
		expErr         error
		expResp        *mgmtpb.SystemDrainResp
	}{
		"nil req": {
			req:    (*mgmtpb.SystemDrainReq)(nil),
			expErr: errors.New("nil *mgmt.SystemDrainReq"),
		},
		"not system leader": {
			req:    &mgmtpb.SystemDrainReq{Sys: "quack"},
			expErr: FaultWrongSystem("quack", build.DefaultSystemName),
		},
		"no hosts or ranks": {
			req:    &mgmtpb.SystemDrainReq{},
			expErr: errors.New("no hosts or ranks"),
		},
		"hosts and ranks": {
			req: &mgmtpb.SystemDrainReq{
				Hosts: "host1,host2",
				Ranks: "0,1",
			},
			expErr: errors.New("ranklist and hostlist"),
		},
		"invalid ranks": {
			req:    &mgmtpb.SystemDrainReq{Ranks: "41,42"},
			expErr: errors.New("invalid rank(s)"),
		},
		"invalid hosts": {
			req:    &mgmtpb.SystemDrainReq{Hosts: "host-[1-2]"},
			expErr: errors.New("invalid host(s)"),
		},
		"local failure on pool query": {
			req:   &mgmtpb.SystemDrainReq{Ranks: "0"},
			pools: []string{test.MockUUID(1)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Error: errors.New("local failed"),
				},
			},
			expErr:       errors.New("local failed"),
			expDrpcCount: 1,
		},
		"local failure on pool drain": {
			req:   &mgmtpb.SystemDrainReq{Ranks: "0"},
			pools: []string{test.MockUUID(1)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "0-4",
					},
				},
			},
			mic: &control.MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expResp: &mgmtpb.SystemDrainResp{
				Responses: []*mgmtpb.PoolRanksResp{
					{
						Id: test.MockUUID(1),
						Results: []*sharedpb.RankResult{
							{
								Rank:    0,
								Errored: true,
								Msg:     "local failed",
							},
						},
					},
				},
			},
			expDrpcCount: 1,
		},
		"remote failure on pool drain": {
			req:   &mgmtpb.SystemDrainReq{Ranks: "0"},
			pools: []string{test.MockUUID(1)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "0-4",
					},
				},
			},
			mic: &control.MockInvokerConfig{
				UnaryResponseSet: []*control.UnaryResponse{
					control.MockMSResponse("host1", errors.New("remote failed"),
						nil),
				},
			},
			expResp: &mgmtpb.SystemDrainResp{
				Responses: []*mgmtpb.PoolRanksResp{
					{
						Id: test.MockUUID(1),
						Results: []*sharedpb.RankResult{
							{
								Rank:    0,
								Errored: true,
								Msg:     "remote failed",
							},
						},
					},
				},
			},
			expDrpcCount:   1,
			expCtlApiCount: 1,
		},
		"drain single rank on one pool": {
			req:   &mgmtpb.SystemDrainReq{Ranks: "0"},
			pools: []string{test.MockUUID(1), test.MockUUID(2)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "0-4",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "1-7",
					},
				},
			},
			mic: &control.MockInvokerConfig{
				UnaryResponseSet: []*control.UnaryResponse{
					control.MockMSResponse("host1", nil,
						&mgmtpb.PoolDrainResp{}),
				},
			},
			expResp: &mgmtpb.SystemDrainResp{
				Responses: []*mgmtpb.PoolRanksResp{
					{
						Id: test.MockUUID(1),
						Results: []*sharedpb.RankResult{
							{Rank: 0},
						},
					},
				},
			},
			expDrpcCount:   2,
			expCtlApiCount: 1,
		},
		"reintegrate single rank on one pool": {
			req:   &mgmtpb.SystemDrainReq{Ranks: "0", Reint: true},
			pools: []string{test.MockUUID(1), test.MockUUID(2)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "0-4",
						DisabledRanks: "",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "1-7",
						DisabledRanks: "0,8",
					},
				},
			},
			mic: &control.MockInvokerConfig{
				UnaryResponseSet: []*control.UnaryResponse{
					control.MockMSResponse("host1", nil,
						&mgmtpb.PoolReintResp{}),
				},
			},
			expResp: &mgmtpb.SystemDrainResp{
				Reint: true,
				Responses: []*mgmtpb.PoolRanksResp{
					{
						Id: test.MockUUID(2),
						Results: []*sharedpb.RankResult{
							{Rank: 0},
						},
					},
				},
			},
			expDrpcCount:   2,
			expCtlApiCount: 1,
		},
		"drain multiple ranks on multiple pools": {
			req: &mgmtpb.SystemDrainReq{Ranks: "0-3"},
			members: system.Members{
				system.MockMember(t, 1, system.MemberStateJoined),
				system.MockMember(t, 2, system.MemberStateJoined),
				system.MockMember(t, 3, system.MemberStateJoined),
			},
			pools: []string{test.MockUUID(1), test.MockUUID(2)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "0,2,4",
						DisabledRanks: "1,3,5",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "1-7",
						DisabledRanks: "0,8",
					},
				},
			},
			expResp: &mgmtpb.SystemDrainResp{
				Responses: []*mgmtpb.PoolRanksResp{
					{
						Id: test.MockUUID(1),
						Results: []*sharedpb.RankResult{
							{Rank: 0}, {Rank: 2},
						},
					},
					{
						Id: test.MockUUID(2),
						Results: []*sharedpb.RankResult{
							{Rank: 1}, {Rank: 2}, {Rank: 3},
						},
					},
				},
			},
			expDrpcCount:   2,
			expCtlApiCount: 5,
		},
		"reintegrate multiple ranks on multiple pools": {
			req: &mgmtpb.SystemDrainReq{Ranks: "0-3", Reint: true},
			members: system.Members{
				system.MockMember(t, 1, system.MemberStateJoined),
				system.MockMember(t, 2, system.MemberStateJoined),
				system.MockMember(t, 3, system.MemberStateJoined),
			},
			pools: []string{test.MockUUID(1), test.MockUUID(2)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "0,2,4",
						DisabledRanks: "1,3,5",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "1-7",
						DisabledRanks: "0,8",
					},
				},
			},
			expResp: &mgmtpb.SystemDrainResp{
				Reint: true,
				Responses: []*mgmtpb.PoolRanksResp{
					{
						Id: test.MockUUID(1),
						Results: []*sharedpb.RankResult{
							{Rank: 1}, {Rank: 3},
						},
					},
					{
						Id: test.MockUUID(2),
						Results: []*sharedpb.RankResult{
							{Rank: 0},
						},
					},
				},
			},
			expDrpcCount:   2,
			expCtlApiCount: 3,
		},
		"drain ranks on multiple pools; errored control api call": {
			req: &mgmtpb.SystemDrainReq{Ranks: "0,1"},
			members: system.Members{
				system.MockMember(t, 1, system.MemberStateJoined),
			},
			pools: []string{test.MockUUID(1), test.MockUUID(2)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "0-4",
						DisabledRanks: "5",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "1-7",
						DisabledRanks: "0,8",
					},
				},
			},
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil,
					&mgmtpb.PoolDrainResp{Status: -1}),
			},
			expResp: &mgmtpb.SystemDrainResp{
				Responses: []*mgmtpb.PoolRanksResp{
					{
						Id: test.MockUUID(1),
						Results: []*sharedpb.RankResult{
							{
								Rank:    0,
								Errored: true,
								Msg:     "DER_UNKNOWN(-1): Unknown error code -1",
							},
							{
								Rank:    1,
								Errored: true,
								Msg:     "DER_UNKNOWN(-1): Unknown error code -1",
							},
						},
					},
					{
						Id: test.MockUUID(2),
						Results: []*sharedpb.RankResult{
							{
								Rank:    1,
								Errored: true,
								Msg:     "DER_UNKNOWN(-1): Unknown error code -1",
							},
						},
					},
				},
			},
			expDrpcCount:   2,
			expCtlApiCount: 3,
		},
		"reintegrate rank on multiple pools; use labels": {
			useLabels: true,
			req: &mgmtpb.SystemDrainReq{
				Reint: true,
				// Resolves to ranks 1-2.
				Hosts: fmt.Sprintf("%s,%s", test.MockHostAddr(1),
					test.MockHostAddr(2)),
			},
			members: system.Members{
				system.MockMember(t, 1, system.MemberStateJoined),
				system.MockMember(t, 2, system.MemberStateJoined),
				system.MockMember(t, 3, system.MemberStateJoined),
			},
			pools: []string{test.MockUUID(1), test.MockUUID(2)},
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "0,2,4",
						DisabledRanks: "1,3,5",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks:  "3-7",
						DisabledRanks: "0-2,8",
					},
				},
			},
			expResp: &mgmtpb.SystemDrainResp{
				Reint: true,
				Responses: []*mgmtpb.PoolRanksResp{
					{
						Id: "00000001",
						Results: []*sharedpb.RankResult{
							{Rank: 1},
						},
					},
					{
						Id: "00000002",
						Results: []*sharedpb.RankResult{
							{Rank: 1},
						},
					},
				},
			},
			expDrpcCount:   2,
			expCtlApiCount: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.MustLogContext(t)
			svc := newTestMgmtSvc(t, log)

			for _, m := range tc.members {
				if _, err := svc.membership.Add(m); err != nil {
					t.Fatal(err)
				}
			}

			cfg := new(mockDrpcClientConfig)
			for _, mock := range tc.drpcResps {
				cfg.setSendMsgResponseList(t, mock)
			}
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(svc, 0, mdc)

			mic := tc.mic
			if mic == nil {
				mic = control.DefaultMockInvokerConfig()
			}
			mi := control.NewMockInvoker(log, mic)
			svc.rpcClient = mi

			for _, uuidStr := range tc.pools {
				var label string
				if tc.useLabels {
					label = uuidStr[:8]
				}
				addTestPoolService(t, svc.sysdb, &system.PoolService{
					PoolUUID:  uuid.MustParse(uuidStr),
					PoolLabel: label,
					State:     system.PoolServiceStateReady,
					Replicas:  []ranklist.Rank{0},
				})
			}

			if tc.req != nil && tc.req.Sys == "" {
				tc.req.Sys = build.DefaultSystemName
			}

			gotResp, gotErr := svc.SystemDrain(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)

			if tc.expErr == nil {
				cmpOpts := []cmp.Option{
					cmpopts.IgnoreUnexported(mgmtpb.SystemDrainResp{},
						mgmtpb.PoolRanksResp{}, sharedpb.RankResult{}),
				}
				if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
					t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
				}
			}

			test.AssertEqual(t, tc.expDrpcCount, len(mdc.CalledMethods()),
				"dRPC invoke count")
			test.AssertEqual(t, tc.expCtlApiCount, mi.GetInvokeCount(),
				"rpc client invoke count")
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

			gotResp, gotErr := svc.SystemErase(test.Context(t), req)
			test.ExpectError(t, gotErr, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			checkRankResults(t, tc.expResults, gotResp.Results)
			checkMembers(t, tc.expMembers, svc.membership)
		})
	}
}

func TestServer_MgmtSvc_checkReplaceRank(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())

	for name, tc := range map[string]struct {
		pools         []string
		rankToReplace ranklist.Rank
		drpcResps     []*mockDrpcResponse // Sequential list of dRPC responses.
		expErr        error
	}{
		"nil rank supplied": {
			pools:         []string{test.MockUUID(1), test.MockUUID(2)},
			rankToReplace: ranklist.NilRank,
			expErr:        errors.New("nil rank"),
		},
		"rank in use on a pool": {
			pools:         []string{test.MockUUID(1), test.MockUUID(2)},
			rankToReplace: 5,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "0-4",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "1-7",
					},
				},
			},
			expErr: errors.New("rank 5 is enabled on pool 00000002"),
		},
		"rank not in use on any pools": {
			pools:         []string{test.MockUUID(1), test.MockUUID(2)},
			rankToReplace: 5,
			drpcResps: []*mockDrpcResponse{
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "0-4",
					},
				},
				&mockDrpcResponse{
					Message: &mgmtpb.PoolQueryResp{
						EnabledRanks: "1-4,6,7",
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf.Reset()
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.MustLogContext(t)
			svc := newTestMgmtSvc(t, log)

			for _, uuidStr := range tc.pools {
				addTestPoolService(t, svc.sysdb, &system.PoolService{
					PoolUUID: uuid.MustParse(uuidStr),
					State:    system.PoolServiceStateReady,
					Replicas: []ranklist.Rank{0},
				})
			}

			cfg := new(mockDrpcClientConfig)
			for _, mock := range tc.drpcResps {
				cfg.setSendMsgResponseList(t, mock)
			}
			mdc := newMockDrpcClient(cfg)
			setupSvcDrpcClient(svc, 0, mdc)

			gotErr := svc.checkReplaceRank(ctx, tc.rankToReplace)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestServer_MgmtSvc_Join(t *testing.T) {
	curMember := mockMember(t, 0, 0, "excluded")
	newMember := mockMember(t, 1, 1, "joined")
	newProviderMember := mockMember(t, 1, 1, "joined")
	newProviderMember.PrimaryFabricURI = fmt.Sprintf("verbs://%s", test.MockHostAddr(1))

	for name, tc := range map[string]struct {
		req              *mgmtpb.JoinReq
		pauseGroupUpdate bool
		guResp           *mgmtpb.GroupUpdateResp
		expGuReq         *mgmtpb.GroupUpdateReq
		expResp          *mgmtpb.JoinResp
		expErr           error
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
			expErr: errors.New("invalid fault domain"),
		},
		"dupe host same rank diff uuid": {
			req: &mgmtpb.JoinReq{
				Addr: curMember.Addr.String(),
				Rank: curMember.Rank.Uint32(),
				Uuid: test.MockUUID(5),
			},
			expErr: errors.New("uuid changed"),
		},
		"dupe host diff rank same uuid": {
			req: &mgmtpb.JoinReq{
				Addr: curMember.Addr.String(),
				Rank: 22,
				Uuid: curMember.UUID.String(),
			},
			expErr: errors.New("already exists"),
		},
		"dupe host addr changed": {
			req: &mgmtpb.JoinReq{
				Addr: newMember.Addr.String(),
				Rank: curMember.Rank.Uint32(),
				Uuid: curMember.UUID.String(),
			},
			expErr: errors.New("control address changed"),
		},
		"rejoining host": {
			req: &mgmtpb.JoinReq{
				Addr:        curMember.Addr.String(),
				Rank:        curMember.Rank.Uint32(),
				Uuid:        curMember.UUID.String(),
				Incarnation: curMember.Incarnation + 1,
				Uri:         curMember.PrimaryFabricURI,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 3,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					{
						Rank:        curMember.Rank.Uint32(),
						Uri:         curMember.PrimaryFabricURI, // update URI
						Incarnation: curMember.Incarnation + 1,
					},
				},
			},
			expResp: &mgmtpb.JoinResp{
				Status:     0,
				Rank:       curMember.Rank.Uint32(),
				State:      mgmtpb.JoinResp_IN,
				MapVersion: 2,
			},
		},
		"rejoining host; NilRank": {
			req: &mgmtpb.JoinReq{
				Addr:        curMember.Addr.String(),
				Rank:        uint32(ranklist.NilRank),
				Uuid:        curMember.UUID.String(),
				Incarnation: curMember.Incarnation + 1,
				Uri:         curMember.PrimaryFabricURI,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 3,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					{
						Rank:        curMember.Rank.Uint32(),
						Uri:         curMember.PrimaryFabricURI, // update URI
						Incarnation: curMember.Incarnation + 1,
					},
				},
			},
			expResp: &mgmtpb.JoinResp{
				Status:     0,
				Rank:       curMember.Rank.Uint32(),
				State:      mgmtpb.JoinResp_IN,
				MapVersion: 2,
			},
		},
		"provider doesn't match": {
			pauseGroupUpdate: true,
			req: &mgmtpb.JoinReq{
				Addr:        curMember.Addr.String(),
				Rank:        curMember.Rank.Uint32(),
				Uuid:        curMember.UUID.String(),
				Uri:         newProviderMember.PrimaryFabricURI,
				Incarnation: curMember.Incarnation + 1,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 3,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					{
						Rank:        curMember.Rank.Uint32(),
						Uri:         newProviderMember.PrimaryFabricURI, // update URI
						Incarnation: curMember.Incarnation + 1,
					},
				},
			},
			expErr: errors.New("does not match"),
		},
		"group update resumed": {
			pauseGroupUpdate: true,
			req: &mgmtpb.JoinReq{
				Addr:        curMember.Addr.String(),
				Rank:        curMember.Rank.Uint32(),
				Uuid:        curMember.UUID.String(),
				Uri:         curMember.PrimaryFabricURI,
				Incarnation: curMember.Incarnation + 1,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 3,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					{
						Rank:        curMember.Rank.Uint32(),
						Uri:         curMember.PrimaryFabricURI,
						Incarnation: curMember.Incarnation + 1,
					},
				},
			},
			expResp: &mgmtpb.JoinResp{
				Status:     0,
				Rank:       curMember.Rank.Uint32(),
				State:      mgmtpb.JoinResp_IN,
				MapVersion: 2,
			},
		},
		"new host (non local)": {
			req: &mgmtpb.JoinReq{
				Addr:        curMember.Addr.String(),
				Rank:        uint32(ranklist.NilRank),
				Incarnation: newMember.Incarnation,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 3,
				Engines: []*mgmtpb.GroupUpdateReq_Engine{
					// rank 0 is excluded, so shouldn't be in the map
					{
						Rank:        newMember.Rank.Uint32(),
						Uri:         newMember.PrimaryFabricURI,
						Incarnation: newMember.Incarnation,
					},
				},
			},
			expResp: &mgmtpb.JoinResp{
				Status:     0,
				Rank:       newMember.Rank.Uint32(),
				State:      mgmtpb.JoinResp_IN,
				LocalJoin:  false,
				MapVersion: 2,
			},
		},
		"new host (local)": {
			req: &mgmtpb.JoinReq{
				Addr:        common.LocalhostCtrlAddr().String(),
				Uri:         "tcp://" + common.LocalhostCtrlAddr().String(),
				Rank:        uint32(ranklist.NilRank),
				Incarnation: newMember.Incarnation,
			},
			expGuReq: &mgmtpb.GroupUpdateReq{
				MapVersion: 3,
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
				Status:     0,
				Rank:       newMember.Rank.Uint32(),
				State:      mgmtpb.JoinResp_IN,
				LocalJoin:  true,
				MapVersion: 2,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			// Make a copy to avoid test side-effects.
			curCopy := &system.Member{}
			*curCopy = *curMember
			curCopy.Rank = ranklist.NilRank // ensure that db.data.NextRank is incremented

			svc := mgmtSystemTestSetup(t, log, system.Members{curCopy}, nil)
			if tc.pauseGroupUpdate {
				svc.pauseGroupUpdate()
			}

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
				tc.req.Uri = newMember.PrimaryFabricURI
			}
			if tc.req.SrvFaultDomain == "" {
				tc.req.SrvFaultDomain = newMember.FaultDomain.String()
			}
			if tc.req.Nctxs == 0 {
				tc.req.Nctxs = newMember.PrimaryFabricContexts
			}
			if tc.req.Incarnation == 0 {
				tc.req.Incarnation = newMember.Incarnation
			}
			peerAddr, err := net.ResolveTCPAddr("tcp", tc.req.Addr)
			if err != nil {
				t.Fatal(err)
			}
			peerCtx := peer.NewContext(test.Context(t), &peer.Peer{Addr: peerAddr})

			mdc := getMockDrpcClient(tc.guResp, nil)
			setupSvcDrpcClient(svc, 0, mdc)

			gotResp, gotErr := svc.Join(peerCtx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			if tc.expGuReq == nil {
				return
			}

			gotGuReq := new(mgmtpb.GroupUpdateReq)
			calls := mdc.calls.get()
			// wait for GroupUpdate
			for ; ; calls = mdc.calls.get() {
				if len(calls) == 0 {
					continue
				}
				if calls[len(calls)-1].Method == drpc.MethodGroupUpdate {
					break
				}
			}
			if err := proto.Unmarshal(calls[len(calls)-1].Body, gotGuReq); err != nil {
				t.Fatal(err)
			}
			cmpOpts := cmp.Options{
				protocmp.Transform(),
				protocmp.SortRepeatedFields(&mgmtpb.GroupUpdateReq{}, "engines"),
			}
			if diff := cmp.Diff(tc.expGuReq, gotGuReq, cmpOpts...); diff != "" {
				t.Fatalf("unexpected GroupUpdate request (-want, +got):\n%s", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_doGroupUpdate(t *testing.T) {
	mockMembers := func(t *testing.T, count int, state string) system.Members {
		result := system.Members{}
		for i := 0; i < count; i++ {
			result = append(result, mockMember(t, int32(i), int32(i), state))
		}
		return result
	}

	defaultMemberCount := 3
	defaultTestMS := func(t *testing.T, l logging.Logger) *mgmtSvc {
		return mgmtSystemTestSetup(t, l, mockMembers(t, defaultMemberCount, "joined"), nil)
	}

	uri := func(idx int) string {
		return "tcp://" + test.MockHostAddr(int32(idx)).String()
	}

	getGroupUpdateReq := func(mapVer, count int) *mgmtpb.GroupUpdateReq {
		req := &mgmtpb.GroupUpdateReq{
			MapVersion: uint32(mapVer),
		}
		for i := 0; i < count; i++ {
			req.Engines = append(req.Engines, &mgmtpb.GroupUpdateReq_Engine{
				Rank:        uint32(i),
				Uri:         uri(i),
				Incarnation: uint64(i),
			})
		}
		return req
	}

	getDefaultGroupUpdateReq := func() *mgmtpb.GroupUpdateReq {
		return getGroupUpdateReq(defaultMemberCount, defaultMemberCount)
	}

	for name, tc := range map[string]struct {
		getSvc     func(*testing.T, logging.Logger) *mgmtSvc
		force      bool
		expDrpcReq *mgmtpb.GroupUpdateReq
		drpcResp   *mgmtpb.GroupUpdateResp
		drpcErr    error
		expErr     error
	}{
		"group update paused": {
			getSvc: func(t *testing.T, l logging.Logger) *mgmtSvc {
				svc := defaultTestMS(t, l)
				svc.pauseGroupUpdate()
				return svc
			},
		},
		"group update paused with force": {
			getSvc: func(t *testing.T, l logging.Logger) *mgmtSvc {
				svc := defaultTestMS(t, l)
				svc.pauseGroupUpdate()
				return svc
			},
			force: true,
		},
		"no ranks": {
			getSvc: func(t *testing.T, l logging.Logger) *mgmtSvc {
				svc := mgmtSystemTestSetup(t, l, system.Members{}, nil)
				return svc
			},
			expErr: system.ErrEmptyGroupMap,
		},
		"map version already updated": {
			getSvc: func(t *testing.T, l logging.Logger) *mgmtSvc {
				svc := defaultTestMS(t, l)
				svc.lastMapVer = uint32(defaultMemberCount)
				return svc
			},
		},
		"drpc failed": {
			drpcErr:    errors.New("mock drpc"),
			expDrpcReq: getDefaultGroupUpdateReq(),
			expErr:     errors.New("mock drpc"),
		},
		"drpc bad status": {
			drpcResp:   &mgmtpb.GroupUpdateResp{Status: daos.MiscError.Int32()},
			expDrpcReq: getDefaultGroupUpdateReq(),
			expErr:     daos.MiscError,
		},
		"success": {
			drpcResp:   &mgmtpb.GroupUpdateResp{},
			expDrpcReq: getDefaultGroupUpdateReq(),
		},
		"force": {
			force:      true,
			drpcResp:   &mgmtpb.GroupUpdateResp{},
			expDrpcReq: getGroupUpdateReq(defaultMemberCount+1, defaultMemberCount),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.getSvc == nil {
				tc.getSvc = func(t *testing.T, l logging.Logger) *mgmtSvc {
					svc := defaultTestMS(t, l)
					return svc
				}
			}
			svc := tc.getSvc(t, log)
			mockDrpc := getMockDrpcClient(tc.drpcResp, tc.drpcErr)
			setupSvcDrpcClient(svc, 0, mockDrpc)

			err := svc.doGroupUpdate(test.Context(t), tc.force)

			test.CmpErr(t, tc.expErr, err)

			gotDrpcCalls := mockDrpc.calls.get()
			if tc.expDrpcReq == nil {
				test.AssertEqual(t, 0, len(gotDrpcCalls), "no dRPC calls expected")
			} else {
				test.AssertEqual(t, 1, len(gotDrpcCalls), "expected a GroupUpdate dRPC call")

				gotReq := new(mgmtpb.GroupUpdateReq)
				if err := proto.Unmarshal(gotDrpcCalls[0].Body, gotReq); err != nil {
					t.Fatal(err)
				}

				// Order of engines in the actual req is arbitrary -- sort for comparison
				sort.Slice(gotReq.Engines, func(i, j int) bool {
					return gotReq.Engines[i].Rank < gotReq.Engines[j].Rank
				})
				if diff := cmp.Diff(tc.expDrpcReq, gotReq, cmpopts.IgnoreUnexported(mgmtpb.GroupUpdateReq{}, mgmtpb.GroupUpdateReq_Engine{})); diff != "" {
					t.Fatalf("want-, got+:\n%s", diff)
				}
			}
		})
	}
}

func TestMgmtSvc_verifyFaultDomain(t *testing.T) {
	testURI := "tcp://localhost:10001"
	for name, tc := range map[string]struct {
		getSvc         func(*testing.T, logging.Logger) *mgmtSvc
		curLabels      []string
		req            *mgmtpb.JoinReq
		expFaultDomain *system.FaultDomain
		expErr         error
		expLabels      []string
	}{
		"no fault domain": {
			req:    &mgmtpb.JoinReq{},
			expErr: errors.New("no fault domain"),
		},
		"invalid fault domain": {
			req:    &mgmtpb.JoinReq{SrvFaultDomain: "junk"},
			expErr: errors.New("invalid fault domain"),
		},
		"failed to get system domain labels": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, false)
				// not a replica
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
				})

				return svc
			},
			req:    &mgmt.JoinReq{SrvFaultDomain: "/rack=r1/node=n2"},
			expErr: &system.ErrNotReplica{},
		},
		"failed to set system domain labels": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, true)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
					Replicas:   []*net.TCPAddr{common.LocalhostCtrlAddr()},
				})
				if err := svc.sysdb.ResignLeadership(errors.New("test")); err != nil {
					t.Fatal(err)
				}

				return svc
			},
			req:    &mgmt.JoinReq{SrvFaultDomain: "/rack=r1/node=n2"},
			expErr: &system.ErrNotLeader{},
		},
		"first success with labels": {
			req:            &mgmt.JoinReq{SrvFaultDomain: "/rack=r1/node=n2"},
			expFaultDomain: system.MustCreateFaultDomainFromString("/rack=r1/node=n2"),
			expLabels:      []string{"rack", "node"},
		},
		"first success with no labels": {
			req:            &mgmt.JoinReq{SrvFaultDomain: "/r1/n2"},
			expFaultDomain: system.MustCreateFaultDomainFromString("/r1/n2"),
			expLabels:      []string{"", ""},
		},
		"success with labels": {
			curLabels:      []string{"rack", "node"},
			req:            &mgmt.JoinReq{SrvFaultDomain: "/rack=r1/node=n2"},
			expFaultDomain: system.MustCreateFaultDomainFromString("/rack=r1/node=n2"),
			expLabels:      []string{"rack", "node"},
		},
		"success with no labels": {
			curLabels:      []string{"", ""},
			req:            &mgmt.JoinReq{SrvFaultDomain: "/r1/n2"},
			expFaultDomain: system.MustCreateFaultDomainFromString("/r1/n2"),
			expLabels:      []string{"", ""},
		},
		"labeled request with unlabeled system": {
			curLabels: []string{"", ""},
			req: &mgmt.JoinReq{
				SrvFaultDomain: "/rack=r1/node=n2",
				Uri:            testURI,
			},
			expErr:    FaultBadFaultDomainLabels("/rack=r1/node=n2", testURI, []string{"rack", "node"}, nil),
			expLabels: []string{"", ""},
		},
		"unlabeled request with labeled system": {
			curLabels: []string{"rack", "node"},
			req: &mgmt.JoinReq{
				SrvFaultDomain: "/r1/n2",
				Uri:            testURI,
			},
			expErr:    FaultBadFaultDomainLabels("/r1/n2", testURI, nil, []string{"rack", "node"}),
			expLabels: []string{"rack", "node"},
		},
		"mismatched labels": {
			curLabels: []string{"rack", "node"},
			req: &mgmt.JoinReq{
				SrvFaultDomain: "/rack=r1/host=n2",
				Uri:            testURI,
			},
			expErr:    FaultBadFaultDomainLabels("/rack=r1/host=n2", testURI, []string{"rack", "host"}, []string{"rack", "node"}),
			expLabels: []string{"rack", "node"},
		},
		"mismatched length": {
			curLabels: []string{"rack"},
			req: &mgmt.JoinReq{
				SrvFaultDomain: "/rack=r1/node=n2",
				Uri:            testURI,
			},
			expErr:    FaultBadFaultDomainLabels("/rack=r1/node=n2", testURI, []string{"rack", "node"}, []string{"rack"}),
			expLabels: []string{"rack"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.getSvc == nil {
				tc.getSvc = func(t *testing.T, l logging.Logger) *mgmtSvc {
					svc := mgmtSystemTestSetup(t, l,
						system.Members{
							mockMember(t, 1, 1, "stopped"),
							mockMember(t, 2, 2, "stopped"),
						},
						[]*control.HostResponse{})
					return svc
				}
			}
			svc := tc.getSvc(t, log)
			if tc.curLabels != nil {
				if err := svc.setDomainLabels(tc.curLabels); err != nil {
					t.Fatal(err)
				}
			}

			fd, err := svc.verifyFaultDomain(tc.req)

			test.CmpErr(t, tc.expErr, err)
			test.AssertTrue(t, fd.Equals(tc.expFaultDomain), fmt.Sprintf("want %q, got %q", tc.expFaultDomain, fd))

			if tc.expLabels == nil {
				return
			}

			newLabels, labelErr := svc.getDomainLabels()
			if len(tc.expLabels) == 0 {
				test.AssertTrue(t, system.IsErrSystemAttrNotFound(labelErr), "")
			} else if labelErr != nil {
				t.Fatal(labelErr)
			}
			test.CmpAny(t, "", tc.expLabels, newLabels)
		})
	}
}

func TestMgmtSvc_updateFabricProviders(t *testing.T) {
	for name, tc := range map[string]struct {
		getSvc               func(*testing.T, logging.Logger) *mgmtSvc
		oldProv              string
		provs                []string
		expErr               error
		expProv              string
		expNumEvents         int
		expGroupUpdatePaused bool
	}{
		"no change": {
			oldProv: "tcp",
			provs:   []string{"tcp"},
			expProv: "tcp",
		},
		"successful change": {
			oldProv:              "tcp",
			provs:                []string{"verbs"},
			expProv:              "verbs",
			expNumEvents:         1,
			expGroupUpdatePaused: true,
		},
		"fails getting prop": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, false)
				// not a replica
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
				})

				return svc
			},
			provs:  []string{"verbs"},
			expErr: &system.ErrNotReplica{},
		},
		"change fails setting prop": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, true)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
					Replicas:   []*net.TCPAddr{common.LocalhostCtrlAddr()},
				})
				if err := svc.setFabricProviders("tcp"); err != nil {
					t.Fatal(err)
				}
				if err := svc.sysdb.ResignLeadership(errors.New("test")); err != nil {
					t.Fatal(err)
				}

				return svc
			},
			oldProv: "tcp",
			provs:   []string{"verbs"},
			expErr:  &system.ErrNotLeader{},
		},
		"first time": {
			provs:   []string{"verbs"},
			expProv: "verbs",
		},
		"first time fails setting prop": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, true)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
					Replicas:   []*net.TCPAddr{common.LocalhostCtrlAddr()},
				})
				if err := svc.sysdb.ResignLeadership(errors.New("test")); err != nil {
					t.Fatal(err)
				}

				return svc
			},
			provs:  []string{"verbs"},
			expErr: &system.ErrNotLeader{},
		},
		"member already joined": {
			getSvc: func(t *testing.T, l logging.Logger) *mgmtSvc {
				ms := mgmtSystemTestSetup(t, l,
					system.Members{mockMember(t, 1, 1, "joined")},
					[]*control.HostResponse{})
				if err := ms.setFabricProviders("tcp"); err != nil {
					t.Fatal(err)
				}
				return ms
			},
			provs:  []string{"verbs"},
			expErr: errors.New("already joined"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.getSvc == nil {
				tc.getSvc = func(t *testing.T, l logging.Logger) *mgmtSvc {
					ms := mgmtSystemTestSetup(t, l,
						system.Members{
							mockMember(t, 1, 1, "stopped"),
							mockMember(t, 2, 2, "stopped"),
						},
						[]*control.HostResponse{})
					if err := ms.setFabricProviders(tc.oldProv); err != nil {
						t.Fatal(err)
					}
					return ms
				}
			}

			svc := tc.getSvc(t, log)
			mockPub := &mockPublisher{}

			err := svc.updateFabricProviders(tc.provs, mockPub)

			test.CmpErr(t, tc.expErr, err)

			t.Logf("published events:\n%+v", mockPub.published)
			test.AssertEqual(t, tc.expNumEvents, len(mockPub.published), "unexpected number of events published")

			if tc.expNumEvents > 0 {
				gotEvent := mockPub.published[0]
				test.AssertEqual(t, events.RASSystemFabricProvChanged, gotEvent.ID, "")
				test.AssertEqual(t, events.RASSeverityNotice, gotEvent.Severity, "")
			}

			if tc.expProv != "" {
				curProv, err := svc.getFabricProvider()
				if err != nil {
					t.Fatal(err)
				}
				test.AssertEqual(t, tc.expProv, curProv, "")
			}
			test.AssertEqual(t, tc.expGroupUpdatePaused, svc.isGroupUpdatePaused(), "")
		})
	}
}

func TestMgmtSvc_checkReqFabricProvider(t *testing.T) {
	sysProv := "tcp"
	for name, tc := range map[string]struct {
		getSvc       func(*testing.T, logging.Logger) *mgmtSvc
		joinProv     string
		joinURI      string
		expErr       error
		expNumEvents int
	}{
		"bad URI": {
			joinURI: "bad format",
			expErr:  errors.New("unable to parse fabric provider"),
		},
		"failed getting system provider": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, false)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
				})

				return svc
			},
			joinURI: "tcp://10.10.10.10",
			expErr:  &system.ErrNotReplica{},
		},
		"prop not set": {
			getSvc: func(t *testing.T, l logging.Logger) *mgmtSvc {
				ms := mgmtSystemTestSetup(t, l, system.Members{}, []*control.HostResponse{})
				if err := ms.setFabricProviders(""); err != nil {
					t.Fatal(err)
				}
				return ms
			},
			joinProv: "tcp",
			joinURI:  "tcp://10.10.10.10",
			expErr:   system.ErrLeaderStepUpInProgress,
		},
		"success": {
			joinProv: "tcp",
			joinURI:  "tcp://10.10.10.10",
		},
		"does not match": {
			joinProv:     "verbs",
			joinURI:      "verbs://10.10.10.10",
			expErr:       errors.New("does not match"),
			expNumEvents: 1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.getSvc == nil {
				tc.getSvc = func(t *testing.T, l logging.Logger) *mgmtSvc {
					ms := mgmtSystemTestSetup(t, l, system.Members{}, []*control.HostResponse{})
					if err := ms.setFabricProviders(sysProv); err != nil {
						t.Fatal(err)
					}
					return ms
				}
			}
			svc := tc.getSvc(t, log)
			mockPub := &mockPublisher{}

			req := &mgmtpb.JoinReq{
				Uri:  tc.joinURI,
				Rank: 12,
			}
			addr := &net.TCPAddr{
				IP:   net.IPv4(1, 2, 3, 4),
				Port: 5678,
			}
			err := svc.checkReqFabricProvider(req, addr, mockPub)

			test.CmpErr(t, tc.expErr, err)

			t.Logf("published events:\n%+v", mockPub.published)
			test.AssertEqual(t, tc.expNumEvents, len(mockPub.published), "unexpected number of events published")

			if tc.expNumEvents > 0 {
				gotEvent := mockPub.published[0]
				test.AssertEqual(t, events.RASEngineJoinFailed, gotEvent.ID, "")
				test.AssertEqual(t, events.RASSeverityError, gotEvent.Severity, "")
				test.AssertEqual(t, req.Rank, gotEvent.Rank, "")
				test.AssertEqual(t, addr.String(), gotEvent.Hostname, "")
			}
		})
	}
}

func TestMgmtSvc_isGroupUpdatePaused(t *testing.T) {
	for name, tc := range map[string]struct {
		getSvc    func(*testing.T, logging.Logger) *mgmtSvc
		propVal   string
		expResult bool
	}{
		"not leader": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, false)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
				})

				return svc
			},
		},
		"never set": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return mgmtSystemTestSetup(t, log, system.Members{}, []*control.HostResponse{})
			},
		},
		"empty string": {},
		"true": {
			propVal:   "true",
			expResult: true,
		},
		"true numeric": {
			propVal:   "1",
			expResult: true,
		},
		"false": {
			propVal: "false",
		},
		"false numeric": {
			propVal: "0",
		},
		"garbage": {
			propVal: "blah blah blah",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.getSvc == nil {
				tc.getSvc = func(t *testing.T, l logging.Logger) *mgmtSvc {
					ms := mgmtSystemTestSetup(t, l, system.Members{}, []*control.HostResponse{})
					if err := system.SetMgmtProperty(ms.sysdb, groupUpdatePauseProp, tc.propVal); err != nil {
						t.Fatal(err)
					}
					return ms
				}
			}

			svc := tc.getSvc(t, log)

			test.AssertEqual(t, tc.expResult, svc.isGroupUpdatePaused(), "")
		})
	}
}

func TestMgmtSvc_pauseGroupUpdate(t *testing.T) {
	for name, tc := range map[string]struct {
		getSvc   func(*testing.T, logging.Logger) *mgmtSvc
		startVal string
		expErr   error
	}{
		"not leader": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, false)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
				})

				return svc
			},
			expErr: &system.ErrNotReplica{},
		},
		"never set": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return mgmtSystemTestSetup(t, log, system.Members{}, []*control.HostResponse{})
			},
		},
		"true": {
			startVal: "true",
		},
		"false": {
			startVal: "false",
		},
		"garbage": {
			startVal: "blah blah blah",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.getSvc == nil {
				tc.getSvc = func(t *testing.T, l logging.Logger) *mgmtSvc {
					ms := mgmtSystemTestSetup(t, l, system.Members{}, []*control.HostResponse{})
					if err := system.SetMgmtProperty(ms.sysdb, groupUpdatePauseProp, tc.startVal); err != nil {
						t.Fatal(err)
					}
					return ms
				}
			}

			svc := tc.getSvc(t, log)

			err := svc.pauseGroupUpdate()

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expErr == nil, svc.isGroupUpdatePaused(), "")
		})
	}
}

func TestMgmtSvc_resumeGroupUpdate(t *testing.T) {
	for name, tc := range map[string]struct {
		getSvc   func(*testing.T, logging.Logger) *mgmtSvc
		startVal string
		expErr   error
	}{
		"not leader": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				svc := newTestMgmtSvcMulti(t, log, maxEngines, false)
				svc.sysdb = raft.MockDatabaseWithCfg(t, log, &raft.DatabaseConfig{
					SystemName: build.DefaultSystemName,
				})

				return svc
			},
			expErr: &system.ErrNotReplica{},
		},
		"never set": {
			getSvc: func(t *testing.T, log logging.Logger) *mgmtSvc {
				return mgmtSystemTestSetup(t, log, system.Members{}, []*control.HostResponse{})
			},
		},
		"true": {
			startVal: "true",
		},
		"false": {
			startVal: "false",
		},
		"garbage": {
			startVal: "blah blah blah",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.getSvc == nil {
				tc.getSvc = func(t *testing.T, l logging.Logger) *mgmtSvc {
					ms := mgmtSystemTestSetup(t, l, system.Members{}, []*control.HostResponse{})
					if err := system.SetMgmtProperty(ms.sysdb, groupUpdatePauseProp, tc.startVal); err != nil {
						t.Fatal(err)
					}
					return ms
				}
			}

			svc := tc.getSvc(t, log)

			err := svc.resumeGroupUpdate()

			test.CmpErr(t, tc.expErr, err)
			test.AssertFalse(t, svc.isGroupUpdatePaused(), "")
		})
	}
}
