//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	. "github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	. "github.com/daos-stack/daos/src/control/system"
)

func getTestNotifyReadyReq(t *testing.T, sockPath string, idx uint32) *srvpb.NotifyReadyReq {
	return &srvpb.NotifyReadyReq{
		DrpcListenerSock: sockPath,
		InstanceIdx:      idx,
	}
}

func waitForEngineReady(t *testing.T, instance *EngineInstance) {
	select {
	case <-time.After(100 * time.Millisecond):
		t.Fatal("IO engine never became ready!")
	case <-instance.awaitDrpcReady():
		return
	}
}

func TestEngineInstance_NotifyDrpcReady(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	instance := getTestEngineInstance(log)

	req := getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0)

	instance.NotifyDrpcReady(req)

	dc, err := instance.getDrpcClient()
	if err != nil || dc == nil {
		t.Fatal("Expected a dRPC client connection")
	}

	waitForEngineReady(t, instance)
}

func TestEngineInstance_CallDrpc(t *testing.T) {
	for name, tc := range map[string]struct {
		notStarted bool
		notReady   bool
		noClient   bool
		resp       *drpc.Response
		expErr     error
	}{
		"not started": {
			notStarted: true,
			expErr:     FaultDataPlaneNotStarted,
		},
		"not ready": {
			notReady: true,
			expErr:   errEngineNotReady,
		},
		"no client configured": {
			noClient: true,
			expErr:   errDRPCNotReady,
		},
		"success": {
			resp: &drpc.Response{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			trc := engine.TestRunnerConfig{}
			trc.Running.Store(!tc.notStarted)
			runner := engine.NewTestRunner(&trc, engine.MockConfig())
			instance := NewEngineInstance(log, nil, nil, runner)
			instance.ready.Store(!tc.notReady)

			if !tc.noClient {
				cfg := &mockDrpcClientConfig{
					SendMsgResponse: tc.resp,
				}
				instance.setDrpcClient(newMockDrpcClient(cfg))
			}

			_, err := instance.CallDrpc(test.Context(t),
				drpc.MethodPoolCreate, &mgmtpb.PoolCreateReq{})
			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestEngineInstance_DrespToRankResult(t *testing.T) {
	dRank := Rank(1)

	for name, tc := range map[string]struct {
		daosResp    *mgmtpb.DaosResp
		inErr       error
		targetState MemberState
		junkRPC     bool
		expResult   *MemberResult
	}{
		"rank success": {
			expResult: &MemberResult{Rank: dRank, State: MemberStateJoined},
		},
		"rank failure": {
			daosResp: &mgmtpb.DaosResp{Status: int32(daos.NoSpace)},
			expResult: &MemberResult{
				Rank: dRank, State: MemberStateErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d: %s", dRank, daos.NoSpace),
			},
		},
		"drpc failure": {
			inErr: errors.New("returned from CallDrpc"),
			expResult: &MemberResult{
				Rank: dRank, State: MemberStateErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC failed: returned from CallDrpc", dRank),
			},
		},
		"unmarshal failure": {
			junkRPC: true,
			expResult: &MemberResult{
				Rank: dRank, State: MemberStateErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC unmarshal failed", dRank),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.daosResp == nil {
				tc.daosResp = &mgmtpb.DaosResp{Status: 0}
			}
			if tc.targetState == MemberStateUnknown {
				tc.targetState = MemberStateJoined
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

			gotResult := drespToMemberResult(Rank(dRank), resp, tc.inErr, tc.targetState)
			if diff := cmp.Diff(tc.expResult, gotResult, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
