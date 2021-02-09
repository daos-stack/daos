//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/system"
)

func getTestNotifyReadyReq(t *testing.T, sockPath string, idx uint32) *srvpb.NotifyReadyReq {
	return &srvpb.NotifyReadyReq{
		DrpcListenerSock: sockPath,
		InstanceIdx:      idx,
	}
}

func waitForIosrvReady(t *testing.T, instance *IOServerInstance) {
	select {
	case <-time.After(100 * time.Millisecond):
		t.Fatal("IO server never became ready!")
	case <-instance.awaitDrpcReady():
		return
	}
}

func TestIOServerInstance_NotifyDrpcReady(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	instance := getTestIOServerInstance(log)

	req := getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0)

	instance.NotifyDrpcReady(req)

	dc, err := instance.getDrpcClient()
	if err != nil || dc == nil {
		t.Fatal("Expected a dRPC client connection")
	}

	waitForIosrvReady(t, instance)
}

func TestIOServerInstance_CallDrpc(t *testing.T) {
	for name, tc := range map[string]struct {
		notReady bool
		resp     *drpc.Response
		expErr   error
	}{
		"not ready": {
			notReady: true,
			expErr:   errors.New("no dRPC client set"),
		},
		"success": {
			resp: &drpc.Response{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)
			instance := getTestIOServerInstance(log)
			if !tc.notReady {
				cfg := &mockDrpcClientConfig{
					SendMsgResponse: tc.resp,
				}
				instance.setDrpcClient(newMockDrpcClient(cfg))
			}

			_, err := instance.CallDrpc(context.TODO(),
				drpc.MethodPoolCreate, &mgmtpb.PoolCreateReq{})
			common.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestIOServerInstance_DrespToRankResult(t *testing.T) {
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
			daosResp: &mgmtpb.DaosResp{Status: int32(drpc.DaosNoSpace)},
			expResult: &MemberResult{
				Rank: dRank, State: MemberStateErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d: %s", dRank, drpc.DaosNoSpace),
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
			defer common.ShowBufferOnFailure(t, buf)

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
			if diff := cmp.Diff(tc.expResult, gotResult, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
