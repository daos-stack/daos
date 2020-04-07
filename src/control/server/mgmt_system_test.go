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
	"fmt"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	. "github.com/daos-stack/daos/src/control/system"
)

const (
	// test aliases for member states
	msStarted = uint32(MemberStateStarted)
	msStopped = uint32(MemberStateStopped)
	msErrored = uint32(MemberStateErrored)
)

func TestMgmtSvc_LeaderQuery(t *testing.T) {
	missingSB := newTestMgmtSvc(nil)
	missingSB.harness.instances[0]._superblock = nil
	missingAPs := newTestMgmtSvc(nil)
	missingAPs.harness.instances[0].msClient.cfg.AccessPoints = nil

	for name, tc := range map[string]struct {
		mgmtSvc *mgmtSvc
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
		"no i/o servers": {
			mgmtSvc: newMgmtSvc(NewIOServerHarness(nil), nil, nil),
			req:     &mgmtpb.LeaderQueryReq{},
			expErr:  errors.New("no I/O servers"),
		},
		"missing superblock": {
			mgmtSvc: missingSB,
			req:     &mgmtpb.LeaderQueryReq{},
			expErr:  errors.New("no I/O superblock"),
		},
		"fail to get current leader address": {
			mgmtSvc: missingAPs,
			req:     &mgmtpb.LeaderQueryReq{},
			expErr:  errors.New("current leader address"),
		},
		"successful query": {
			req: &mgmtpb.LeaderQueryReq{},
			expResp: &mgmtpb.LeaderQueryResp{
				CurrentLeader: "localhost",
				Replicas:      []string{"localhost"},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.mgmtSvc == nil {
				tc.mgmtSvc = newTestMgmtSvc(log)
			}

			gotResp, gotErr := tc.mgmtSvc.LeaderQuery(context.TODO(), tc.req)
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

func TestMgmtSvc_DrespToRankResult(t *testing.T) {
	dRank := Rank(1)

	for name, tc := range map[string]struct {
		daosResp    *mgmtpb.DaosResp
		inErr       error
		targetState MemberState
		junkRPC     bool
		expResult   *mgmtpb.RanksResp_RankResult
	}{
		"rank success": {
			expResult: &mgmtpb.RanksResp_RankResult{
				Rank: dRank.Uint32(), Action: "test", State: msStarted,
			},
		},
		"rank failure": {
			daosResp: &mgmtpb.DaosResp{Status: -1},
			expResult: &mgmtpb.RanksResp_RankResult{
				Rank: dRank.Uint32(), Action: "test", State: msErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC returned DER -1", dRank),
			},
		},
		"drpc failure": {
			inErr: errors.New("returned from CallDrpc"),
			expResult: &mgmtpb.RanksResp_RankResult{
				Rank: dRank.Uint32(), Action: "test", State: msErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC failed: returned from CallDrpc", dRank),
			},
		},
		"unmarshal failure": {
			junkRPC: true,
			expResult: &mgmtpb.RanksResp_RankResult{
				Rank: dRank.Uint32(), Action: "test", State: msErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC unmarshal failed: proto: mgmt.DaosResp: illegal tag 0 (wire type 0)", dRank),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.daosResp == nil {
				tc.daosResp = &mgmtpb.DaosResp{Status: 0}
			}
			if tc.targetState == MemberStateUnknown {
				tc.targetState = MemberStateStarted
			}

			// convert input DaosResp to drpcResponse to test
			rb := makeBadBytes(42)
			if !tc.junkRPC {
				rb, _ = proto.Marshal(tc.daosResp)
			}
			resp := &drpc.Response{
				Status: drpc.Status_SUCCESS, // this will already have been validated by CallDrpc
				Body:   rb,
			}

			gotResult := drespToRankResult(Rank(dRank), "test", resp, tc.inErr, tc.targetState)
			if diff := cmp.Diff(tc.expResult, gotResult, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_PrepShutdownRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		expResp          *mgmtpb.RanksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			missingSB: true,
			req:       &mgmtpb.RanksReq{},
			expErr:    errors.New("nil superblock"),
		},
		"instances stopped": {
			req:              &mgmtpb.RanksReq{},
			instancesStopped: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: msStopped},
					{Rank: 2, Action: "prep shutdown", State: msStopped},
				},
			},
		},
		"dRPC resp fails": {
			req:     &mgmtpb.RanksReq{},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: msErrored, Errored: true},
					{Rank: 2, Action: "prep shutdown", State: msErrored, Errored: true},
				},
			},
		},
		"dRPC resp junk": {
			req:      &mgmtpb.RanksReq{},
			junkResp: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: msErrored, Errored: true},
					{Rank: 2, Action: "prep shutdown", State: msErrored, Errored: true},
				},
			},
		},
		"unsuccessful call": {
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: msErrored, Errored: true},
					{Rank: 2, Action: "prep shutdown", State: msErrored, Errored: true},
				},
			},
		},
		"successful call": {
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "prep shutdown", State: uint32(MemberStateStopping)},
					{Rank: 2, Action: "prep shutdown", State: uint32(MemberStateStopping)},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &ioserver.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.SetIndex(uint32(i))

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
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))

				srv.ready.SetTrue()
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.PrepShutdownRanks(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestMgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				if path.Last().String() == ".Msg" {
					return true
				}
				return false
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResp, gotResp, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_StopRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		expResp          *mgmtpb.RanksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			missingSB: true,
			req:       &mgmtpb.RanksReq{},
			expErr:    errors.New("nil superblock"),
		},
		"dRPC resp fails": { // doesn't effect result, err logged
			req:     &mgmtpb.RanksReq{},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: msStarted, Errored: true},
					{Rank: 2, Action: "stop", State: msStarted, Errored: true},
				},
			},
		},
		"dRPC resp junk": { // doesn't effect result, err logged
			req:      &mgmtpb.RanksReq{},
			junkResp: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: msStarted, Errored: true},
					{Rank: 2, Action: "stop", State: msStarted, Errored: true},
				},
			},
		},
		"unsuccessful call": { // doesn't effect result, err logged
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: msStarted, Errored: true},
					{Rank: 2, Action: "stop", State: msStarted, Errored: true},
				},
			},
		},
		"instances started": { // unsuccessful result for kill
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: msStarted, Errored: true},
					{Rank: 2, Action: "stop", State: msStarted, Errored: true},
				},
			},
		},
		"instances stopped": { // successful result for kill
			req:              &mgmtpb.RanksReq{},
			instancesStopped: true,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "stop", State: msStopped},
					{Rank: 2, Action: "stop", State: msStopped},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &ioserver.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.SetIndex(uint32(i))

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
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))

				srv.ready.SetTrue()
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.StopRanks(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestMgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				if path.Last().String() == ".Msg" {
					return true
				}
				return false
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResp, gotResp, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_PingRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP          bool
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		responseDelay    time.Duration
		expResp          *mgmtpb.RanksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			missingSB: true,
			req:       &mgmtpb.RanksReq{},
			expErr:    errors.New("nil superblock"),
		},
		"instances stopped": {
			req:              &mgmtpb.RanksReq{},
			instancesStopped: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: msStopped},
					{Rank: 2, Action: "ping", State: msStopped},
				},
			},
		},
		"dRPC resp fails": {
			req:     &mgmtpb.RanksReq{},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: msErrored, Errored: true},
					{Rank: 2, Action: "ping", State: msErrored, Errored: true},
				},
			},
		},
		"dRPC resp junk": {
			req:      &mgmtpb.RanksReq{},
			junkResp: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: msErrored, Errored: true},
					{Rank: 2, Action: "ping", State: msErrored, Errored: true},
				},
			},
		},
		"unsuccessful call": {
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: msErrored, Errored: true},
					{Rank: 2, Action: "ping", State: msErrored, Errored: true},
				},
			},
		},
		"successful call": {
			req: &mgmtpb.RanksReq{},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: msStarted},
					{Rank: 2, Action: "ping", State: msStarted},
				},
			},
		},
		"ping timeout": {
			req:           &mgmtpb.RanksReq{},
			responseDelay: 200 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{Rank: 1, Action: "ping", State: uint32(MemberStateUnresponsive), Errored: true},
					{Rank: 2, Action: "ping", State: uint32(MemberStateUnresponsive), Errored: true},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &ioserver.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.SetIndex(uint32(i))

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

				srv.ready.SetTrue()
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.PingRanks(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// RankResult.Msg generation is tested in
			// TestMgmtSvc_DrespToRankResult unit tests
			isMsgField := func(path cmp.Path) bool {
				if path.Last().String() == ".Msg" {
					return true
				}
				return false
			}
			opts := append(common.DefaultCmpOpts(),
				cmp.FilterPath(isMsgField, cmp.Ignore()))

			if diff := cmp.Diff(tc.expResp, gotResp, opts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestMgmtSvc_StartRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		missingSB        bool
		instancesStopped bool
		req              *mgmtpb.RanksReq
		expResp          *mgmtpb.RanksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"missing superblock": {
			missingSB:        true,
			instancesStopped: true,
			req:              &mgmtpb.RanksReq{},
			expErr:           errors.New("nil superblock"),
		},
		"instances started": {
			req: &mgmtpb.RanksReq{},
			expErr: FaultInstancesNotStopped(
				[]*Rank{NewRankPtr(1), NewRankPtr(2)}),
		},
		"instances stopped": { // unsuccessful result for kill
			req:              &mgmtpb.RanksReq{},
			instancesStopped: true,
			expResp: &mgmtpb.RanksResp{
				Results: []*mgmtpb.RanksResp_RankResult{
					{
						Rank: 1, Action: "start", State: msStopped,
						Errored: true, Msg: "want Started, got Stopped",
					},
					{
						Rank: 2, Action: "start", State: msStopped,
						Errored: true, Msg: "want Started, got Stopped",
					},
				},
			},
		},
		// TODO: test instance state changing to started after restart
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := maxIOServers
			svc := newTestMgmtSvcMulti(log, ioserverCount, false)

			svc.harness.started.SetTrue()

			for i, srv := range svc.harness.instances {
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &ioserver.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
				}
				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.SetIndex(uint32(i))

				srv._superblock.Rank = new(Rank)
				*srv._superblock.Rank = Rank(i + 1)

				srv.ready.SetTrue()
			}

			svc.harness.rankReqTimeout = 50 * time.Millisecond

			gotResp, gotErr := svc.StartRanks(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
