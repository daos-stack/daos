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
	"context"
	"net"
	"os"
	"sync"
	"syscall"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
	. "github.com/daos-stack/daos/src/control/system"
)

const (
	// test aliases for member states
	msReady      = uint32(MemberStateReady)
	msWaitFormat = uint32(MemberStateAwaitFormat)
	msStopped    = uint32(MemberStateStopped)
	msErrored    = uint32(MemberStateErrored)
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
				System: "quack",
			},
			expErr: errors.New("wrong system"),
		},
		"successful query": {
			req: &mgmtpb.LeaderQueryReq{System: build.DefaultSystemName},
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

// checkUnorderedRankResults fails if results slices contain any differing results,
// regardless of order. Ignore result "Msg" field as RankResult.Msg generation
// is tested separately in TestServer_MgmtSvc_DrespToRankResult unit tests.
func checkUnorderedRankResults(t *testing.T, expResults, gotResults []*mgmtpb.RanksResp_RankResult) {
	t.Helper()

	isMsgField := func(path cmp.Path) bool {
		return path.Last().String() == ".Msg"
	}
	opts := append(common.DefaultCmpOpts(),
		cmp.FilterPath(isMsgField, cmp.Ignore()))

	common.AssertEqual(t, len(expResults), len(gotResults), "number of rank results")
	for _, exp := range expResults {
		match := false
		for _, got := range gotResults {
			if diff := cmp.Diff(exp, got, opts...); diff == "" {
				match = true
			}
		}
		if !match {
			t.Fatalf("unexpected results: %s", cmp.Diff(expResults, gotResults, opts...))
		}
	}
}

func TestServer_MgmtSvc_PrepShutdownRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		responseDelay    time.Duration
		ctxTimeout       time.Duration
		ctxCancel        time.Duration
		expResults       []*mgmtpb.RanksResp_RankResult
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &mgmtpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &mgmtpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank cannot be read from superblock
			expResults: []*mgmtpb.RanksResp_RankResult{},
		},
		"instances stopped": {
			req:              &mgmtpb.RanksReq{Ranks: "0-3"},
			instancesStopped: true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msStopped},
				{Rank: 2, State: msStopped},
			},
		},
		"dRPC resp fails": {
			req:     &mgmtpb.RanksReq{Ranks: "0-3"},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"dRPC resp junk": {
			req:      &mgmtpb.RanksReq{Ranks: "0-3"},
			junkResp: true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"prep shutdown timeout": { // dRPC req-resp duration > rankReqTime
			req:           &mgmtpb.RanksReq{Ranks: "0-3"},
			responseDelay: 200 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: uint32(MemberStateUnresponsive)},
				{Rank: 2, State: uint32(MemberStateUnresponsive)},
			},
		},
		"context timeout": { // dRPC req-resp duration > parent context timeout
			req:           &mgmtpb.RanksReq{Ranks: "0-3"},
			responseDelay: 40 * time.Millisecond,
			ctxTimeout:    10 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: uint32(MemberStateUnresponsive)},
				{Rank: 2, State: uint32(MemberStateUnresponsive)},
			},
		},
		"context cancel": { // dRPC req-resp duration > when parent context is canceled
			req:           &mgmtpb.RanksReq{Ranks: "0-3"},
			responseDelay: 40 * time.Millisecond,
			ctxCancel:     10 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expErr: errors.New("nil result"), // parent ctx cancel
		},
		"unsuccessful call": {
			req: &mgmtpb.RanksReq{Ranks: "0-3"},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"successful call": {
			req: &mgmtpb.RanksReq{Ranks: "0-3"},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: uint32(MemberStateStopping)},
				{Rank: 2, State: uint32(MemberStateStopping)},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(t, log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &ioserver.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.setIndex(uint32(i))

				srv._superblock.Rank = new(Rank)
				*srv._superblock.Rank = Rank(i + 1)

				cfg := new(mockDrpcClientConfig)
				if tc.drpcRet != nil {
					cfg.setSendMsgResponse(drpc.Status_FAILURE, nil, nil)
				} else if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)

					if tc.responseDelay != time.Duration(0) {
						cfg.setResponseDelay(tc.responseDelay)
					}
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			var cancel context.CancelFunc
			ctx := context.Background()
			if tc.ctxTimeout != 0 {
				ctx, cancel = context.WithTimeout(ctx, tc.ctxTimeout)
				defer cancel()
			} else if tc.ctxCancel != 0 {
				ctx, cancel = context.WithCancel(ctx)
				go func() {
					<-time.After(tc.ctxCancel)
					cancel()
				}()
			}

			gotResp, gotErr := svc.PrepShutdownRanks(ctx, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// order of results nondeterministic as dPrepShutdown run async
			checkUnorderedRankResults(t, tc.expResults, gotResp.Results)
		})
	}
}

func TestServer_MgmtSvc_StopRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		ioserverCount    int
		instancesStopped bool
		req              *mgmtpb.RanksReq
		signal           os.Signal
		signalErr        error
		ctxTimeout       time.Duration
		expSignalsSent   map[uint32]os.Signal
		expResults       []*mgmtpb.RanksResp_RankResult
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &mgmtpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &mgmtpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank cannot be read from superblock
			expResults: []*mgmtpb.RanksResp_RankResult{},
		},
		"missing ranks": {
			req:        &mgmtpb.RanksReq{Ranks: "0,3"},
			expResults: []*mgmtpb.RanksResp_RankResult{},
		},
		"kill signal send error": {
			req: &mgmtpb.RanksReq{
				Ranks: "0-3", Force: true,
			},
			signalErr: errors.New("sending signal failed"),
			expErr:    errors.New("sending killed: sending signal failed"),
		},
		"context timeout": { // near-immediate parent context Timeout
			req:        &mgmtpb.RanksReq{Ranks: "0-3"},
			ctxTimeout: time.Nanosecond,
			expErr:     context.DeadlineExceeded, // parent ctx timeout
		},
		"instances started": { // unsuccessful result for kill
			req:            &mgmtpb.RanksReq{Ranks: "0-3"},
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGINT, 1: syscall.SIGINT},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msReady, Errored: true},
				{Rank: 2, State: msReady, Errored: true},
			},
		},
		"force stop instances started": { // unsuccessful result for kill
			req:            &mgmtpb.RanksReq{Ranks: "0-3", Force: true},
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGKILL, 1: syscall.SIGKILL},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msReady, Errored: true},
				{Rank: 2, State: msReady, Errored: true},
			},
		},
		"instances stopped": { // successful result for kill
			req:              &mgmtpb.RanksReq{Ranks: "0-3"},
			instancesStopped: true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msStopped},
				{Rank: 2, State: msStopped},
			},
		},
		"force stop single instance started": {
			req:            &mgmtpb.RanksReq{Ranks: "1", Force: true},
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGKILL},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msReady, Errored: true},
			},
		},
		"single instance stopped": {
			req:              &mgmtpb.RanksReq{Ranks: "1"},
			instancesStopped: true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msStopped},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			var signalsSent sync.Map

			if tc.ioserverCount == 0 {
				tc.ioserverCount = maxIOServers
			}

			svc := newTestMgmtSvcMulti(t, log, tc.ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &ioserver.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				trc.SignalCb = func(idx uint32, sig os.Signal) { signalsSent.Store(idx, sig) }
				trc.SignalErr = tc.signalErr
				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.setIndex(uint32(i))

				srv._superblock.Rank = new(Rank)
				*srv._superblock.Rank = Rank(i + 1)
			}

			ctx := context.Background()
			if tc.ctxTimeout != 0 {
				var cancel context.CancelFunc
				ctx, cancel = context.WithTimeout(ctx, tc.ctxTimeout)
				defer cancel()
			}
			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.StopRanks(ctx, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestServer_MgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				return path.Last().String() == ".Msg"
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResults, gotResp.Results, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			var numSignalsSent int
			signalsSent.Range(func(_, _ interface{}) bool {
				numSignalsSent++
				return true
			})
			common.AssertEqual(t, len(tc.expSignalsSent), numSignalsSent,
				"number of signals sent")

			for expKey, expValue := range tc.expSignalsSent {
				value, found := signalsSent.Load(expKey)
				if !found {
					t.Fatalf("rank %d was not sent %s signal", expKey, expValue)
				}
				if diff := cmp.Diff(expValue, value); diff != "" {
					t.Fatalf("unexpected signals sent (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestServer_MgmtSvc_PingRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		responseDelay    time.Duration
		ctxTimeout       time.Duration
		ctxCancel        time.Duration
		expResults       []*mgmtpb.RanksResp_RankResult
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &mgmtpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &mgmtpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank can't be read from superblock
			expResults: []*mgmtpb.RanksResp_RankResult{},
		},
		"instances stopped": {
			req:              &mgmtpb.RanksReq{Ranks: "0-3"},
			instancesStopped: true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msStopped},
				{Rank: 2, State: msStopped},
			},
		},
		"instances started": {
			req: &mgmtpb.RanksReq{Ranks: "0-3"},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msReady},
				{Rank: 2, State: msReady},
			},
		},
		"dRPC resp fails": {
			// force flag in request triggers dRPC ping
			req:     &mgmtpb.RanksReq{Ranks: "0-3", Force: true},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"dRPC resp junk": {
			// force flag in request triggers dRPC ping
			req:      &mgmtpb.RanksReq{Ranks: "0-3", Force: true},
			junkResp: true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"dRPC ping timeout": { // dRPC req-resp duration > rankReqTimeout
			// force flag in request triggers dRPC ping
			req:           &mgmtpb.RanksReq{Ranks: "0-3", Force: true},
			responseDelay: 200 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: uint32(MemberStateUnresponsive)},
				{Rank: 2, State: uint32(MemberStateUnresponsive)},
			},
		},
		"dRPC context timeout": { // dRPC req-resp duration > parent context Timeout
			// force flag in request triggers dRPC ping
			req:           &mgmtpb.RanksReq{Ranks: "0-3", Force: true},
			responseDelay: 40 * time.Millisecond,
			ctxTimeout:    10 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: uint32(MemberStateUnresponsive)},
				{Rank: 2, State: uint32(MemberStateUnresponsive)},
			},
		},
		"dRPC context cancel": { // dRPC req-resp duration > when parent context is canceled
			// force flag in request triggers dRPC ping
			req:           &mgmtpb.RanksReq{Ranks: "0-3", Force: true},
			responseDelay: 40 * time.Millisecond,
			ctxCancel:     10 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expErr: errors.New("nil result"), // parent ctx cancel
		},
		"dRPC unsuccessful call": {
			// force flag in request triggers dRPC ping
			req: &mgmtpb.RanksReq{Ranks: "0-3", Force: true},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"dRPC successful call": {
			// force flag in request triggers dRPC ping
			req: &mgmtpb.RanksReq{Ranks: "0-3", Force: true},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msReady},
				{Rank: 2, State: msReady},
			},
		},
		"dRPC filtered ranks": {
			// force flag in request triggers dRPC ping
			req: &mgmtpb.RanksReq{Ranks: "0-1,3", Force: true},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msReady},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(t, log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &ioserver.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.setIndex(uint32(i))

				srv._superblock.Rank = new(Rank)
				*srv._superblock.Rank = Rank(i + 1)

				cfg := new(mockDrpcClientConfig)
				if tc.drpcRet != nil {
					cfg.setSendMsgResponse(drpc.Status_FAILURE, nil, nil)
				} else if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)

					if tc.responseDelay != time.Duration(0) {
						cfg.setResponseDelay(tc.responseDelay)
					}
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			var cancel context.CancelFunc
			ctx := context.Background()
			if tc.ctxTimeout != 0 {
				ctx, cancel = context.WithTimeout(ctx, tc.ctxTimeout)
				defer cancel()
			} else if tc.ctxCancel != 0 {
				ctx, cancel = context.WithCancel(ctx)
				go func() {
					<-time.After(tc.ctxCancel)
					cancel()
				}()
			}

			gotResp, gotErr := svc.PingRanks(ctx, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// order of results nondeterministic as dPing run async
			checkUnorderedRankResults(t, tc.expResults, gotResp.Results)
		})
	}
}

func TestServer_MgmtSvc_ResetFormatRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		ioserverCount    int
		instancesStarted bool
		startFails       bool
		req              *mgmtpb.RanksReq
		ctxTimeout       time.Duration
		expResults       []*mgmtpb.RanksResp_RankResult
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &mgmtpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &mgmtpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank can't be read from superblock
			expResults: []*mgmtpb.RanksResp_RankResult{},
		},
		"missing ranks": {
			req:        &mgmtpb.RanksReq{Ranks: "0,3"},
			expResults: []*mgmtpb.RanksResp_RankResult{},
		},
		"context timeout": { // near-immediate parent context Timeout
			req:        &mgmtpb.RanksReq{Ranks: "0-3"},
			ctxTimeout: time.Nanosecond,
			expErr:     context.DeadlineExceeded, // parent ctx timeout
		},
		"instances already started": {
			req:              &mgmtpb.RanksReq{Ranks: "0-3"},
			instancesStarted: true,
			expErr:           FaultInstancesNotStopped("reset format", 1),
		},
		"instances reach wait format": {
			req: &mgmtpb.RanksReq{Ranks: "0-3"},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msWaitFormat},
				{Rank: 2, State: msWaitFormat},
			},
		},
		"instances stay stopped": {
			req:        &mgmtpb.RanksReq{Ranks: "0-3"},
			startFails: true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msStopped, Errored: true},
				{Rank: 2, State: msStopped, Errored: true},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.ioserverCount == 0 {
				tc.ioserverCount = maxIOServers
			}

			ctx := context.Background()

			svc := newTestMgmtSvcMulti(t, log, tc.ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				testDir, cleanup := common.CreateTestDir(t)
				defer cleanup()
				ioCfg := ioserver.NewConfig().WithScmMountPoint(testDir)

				trc := &ioserver.TestRunnerConfig{}
				if tc.instancesStarted {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, ioCfg)
				srv.setIndex(uint32(i))

				t.Logf("scm dir: %s", srv.scmConfig().MountPoint)
				superblock := &Superblock{
					Version: superblockVersion,
					UUID:    common.MockUUID(),
					System:  "test",
				}
				superblock.Rank = new(system.Rank)
				*superblock.Rank = Rank(i + 1)
				srv.setSuperblock(superblock)
				if err := srv.WriteSuperblock(); err != nil {
					t.Fatal(err)
				}

				// mimic srv.run, set "ready" on startLoop rx
				go func(s *IOServerInstance, startFails bool) {
					<-s.startLoop
					if startFails {
						return
					}
					// processing loop reaches wait for format state
					s.waitFormat.SetTrue()
				}(srv, tc.startFails)
			}

			if tc.ctxTimeout != 0 {
				var cancel context.CancelFunc
				ctx, cancel = context.WithTimeout(ctx, tc.ctxTimeout)
				defer cancel()
			}
			svc.harness.rankStartTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.ResetFormatRanks(ctx, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestServer_MgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				return path.Last().String() == ".Msg"
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResults, gotResp.Results, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_MgmtSvc_StartRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		ioserverCount    int
		instancesStopped bool
		startFails       bool
		req              *mgmtpb.RanksReq
		ctxTimeout       time.Duration
		expResults       []*mgmtpb.RanksResp_RankResult
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &mgmtpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &mgmtpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank cannot be read from superblock
			expResults: []*mgmtpb.RanksResp_RankResult{},
		},
		"missing ranks": {
			req:        &mgmtpb.RanksReq{Ranks: "0,3"},
			expResults: []*mgmtpb.RanksResp_RankResult{},
		},
		"context timeout": { // near-immediate parent context Timeout
			req:        &mgmtpb.RanksReq{Ranks: "0-3"},
			ctxTimeout: time.Nanosecond,
			expErr:     context.DeadlineExceeded, // parent ctx timeout
		},
		"instances already started": {
			req: &mgmtpb.RanksReq{Ranks: "0-3"},
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msReady},
				{Rank: 2, State: msReady},
			},
		},
		"instances get started": {
			req:              &mgmtpb.RanksReq{Ranks: "0-3"},
			instancesStopped: true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msReady},
				{Rank: 2, State: msReady},
			},
		},
		"instances stay stopped": {
			req:              &mgmtpb.RanksReq{Ranks: "0-3"},
			instancesStopped: true,
			startFails:       true,
			expResults: []*mgmtpb.RanksResp_RankResult{
				{Rank: 1, State: msStopped, Errored: true},
				{Rank: 2, State: msStopped, Errored: true},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.ioserverCount == 0 {
				tc.ioserverCount = maxIOServers
			}

			ctx := context.Background()

			svc := newTestMgmtSvcMulti(t, log, tc.ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &ioserver.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.setIndex(uint32(i))

				srv._superblock.Rank = new(Rank)
				*srv._superblock.Rank = Rank(i + 1)

				// mimic srv.run, set "ready" on startLoop rx
				go func(s *IOServerInstance, startFails bool) {
					<-s.startLoop
					t.Logf("instance %d: start signal received", s.Index())
					if startFails {
						return
					}

					// set instance runner started and ready
					ch := make(chan error, 1)
					if err := s.runner.Start(context.TODO(), ch); err != nil {
						t.Logf("failed to start runner: %s", err)
						return
					}
					<-ch
					s.ready.SetTrue()
				}(srv, tc.startFails)
			}

			if tc.ctxTimeout != 0 {
				var cancel context.CancelFunc
				ctx, cancel = context.WithTimeout(ctx, tc.ctxTimeout)
				defer cancel()
			}
			svc.harness.rankStartTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.StartRanks(ctx, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestServer_MgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				return path.Last().String() == ".Msg"
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResults, gotResp.Results, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
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
