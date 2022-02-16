//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/system/checker"
)

type SystemCheckerReq struct {
	unaryRequest
	msRequest
	retryableRequest
}

type SystemCheckerStatusResp struct {
	CurrentPass checker.Pass
	Findings    []*checker.Finding
}

func convertCheckerResp(log debugLogger, ur *UnaryResponse) (*SystemCheckerStatusResp, error) {
	mgmtMsg, err := getMSResponse(ur)
	if err != nil {
		return nil, err
	}

	mgmtResp, ok := mgmtMsg.(*mgmtpb.SystemCheckerStatusResp)
	if !ok {
		return nil, errors.Errorf("unexpected response type: %T", mgmtMsg)
	}
	resp := new(SystemCheckerStatusResp)
	resp.CurrentPass.FromString(mgmtResp.CurrentPass)

	return resp, convert.Types(mgmtResp.Findings, &resp.Findings)
}

// SystemCheckerStart starts the system checker.
func SystemCheckerStart(ctx context.Context, rpcClient UnaryInvoker) (*SystemCheckerStatusResp, error) {
	req := new(SystemCheckerReq)
	pbReq := new(mgmtpb.SystemCheckerStartReq)
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemCheckerStart(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system checker start request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	return convertCheckerResp(rpcClient, ur)
}

// SystemCheckerQuery starts the system checker.
func SystemCheckerQuery(ctx context.Context, rpcClient UnaryInvoker) (*SystemCheckerStatusResp, error) {
	req := new(SystemCheckerReq)
	pbReq := new(mgmtpb.SystemCheckerQueryReq)
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemCheckerQuery(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system checker query request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	return convertCheckerResp(rpcClient, ur)
}
