//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"

	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
)

func TestServer_ControlService_CheckEngineRepair(t *testing.T) {
	testActReq := &mgmtpb.CheckActReq{
		Sys: "sysname",
		Seq: 1234,
		Act: chkpb.CheckInconsistAction_CIA_TRUST_MS,
	}

	rankNums := []uint32{1, 6}

	drpcRespWithMessage := func(t *testing.T, message proto.Message) *drpc.Response {
		mesgBytes, err := proto.Marshal(message)
		if err != nil {
			t.Fatal(err)
		}

		return &drpc.Response{
			Body: mesgBytes,
		}
	}

	for name, tc := range map[string]struct {
		req      *ctlpb.CheckEngineActReq
		drpcErr  error
		drpcResp *drpc.Response
		expErr   error
		expResp  *ctlpb.CheckEngineActResp
	}{
		"nil req": {
			expErr: errors.New("nil"),
		},
		"nil action": {
			req:    &ctlpb.CheckEngineActReq{},
			expErr: errors.New("no CheckActReq"),
		},
		"nil rank": {
			req: &ctlpb.CheckEngineActReq{
				Rank: uint32(ranklist.NilRank),
				Req:  testActReq,
			},
			expErr: errors.New("nil rank"),
		},
		"rank not on server": {
			req: &ctlpb.CheckEngineActReq{
				Rank: 42,
				Req:  testActReq,
			},
			expErr: errors.New("rank 42"),
		},
		"dRPC error": {
			req: &ctlpb.CheckEngineActReq{
				Rank: rankNums[0],
				Req:  testActReq,
			},
			drpcErr: errors.New("mock dRPC Connect error"),
			expErr:  errors.New("mock dRPC Connect error"),
		},
		"dRPC valid response": {
			req: &ctlpb.CheckEngineActReq{
				Rank: rankNums[0],
				Req:  testActReq,
			},
			drpcResp: drpcRespWithMessage(t, &mgmtpb.CheckActResp{Status: daos.MiscError.Int32()}),
			expResp: &ctlpb.CheckEngineActResp{
				Rank: rankNums[0],
				Resp: &mgmtpb.CheckActResp{Status: daos.MiscError.Int32()},
			},
		},
		"dRPC bad response": {
			req: &ctlpb.CheckEngineActReq{
				Rank: rankNums[0],
				Req:  testActReq,
			},
			drpcResp: &drpc.Response{Body: []byte("garbage")},
			expErr:   errors.New("unable to unmarshal"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)
			log := logging.FromContext(ctx)

			cfg := config.DefaultServer().WithEngines(
				engine.MockConfig().WithTargetCount(1),
				engine.MockConfig().WithTargetCount(1),
			)
			svc := mockControlService(t, log, cfg, nil, nil, nil)
			for i, e := range svc.harness.instances {
				srv, ok := e.(*EngineInstance)
				if !ok {
					t.Fatalf("setup error - wrong type for Engine (%T)", e)
				}

				setupTestEngine(t, srv, uint32(i), rankNums[i])

				drpcCfg := new(mockDrpcClientConfig)
				drpcCfg.ConnectError = tc.drpcErr
				drpcCfg.SendMsgResponse = tc.drpcResp

				srv.getDrpcClientFn = func(s string) drpc.DomainSocketClient {
					return newMockDrpcClient(drpcCfg)
				}
			}

			resp, err := svc.CheckEngineRepair(ctx, tc.req)

			test.CmpErr(t, tc.expErr, err)
			test.CmpAny(t, "CheckEngineActResp", tc.expResp, resp, cmpopts.IgnoreUnexported(ctlpb.CheckEngineActResp{}, mgmtpb.CheckActResp{}))
		})
	}
}
