//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/engine"
)

// SetEngineLogMasksReq contains the inputs for the set engine log level
// request.
type SetEngineLogMasksReq struct {
	unaryRequest
	Masks string
}

// SetEngineLogMasksResp contains the results of a set engine log level
// request.
type SetEngineLogMasksResp struct {
	HostErrorsResp
}

// SetEngineLogMasks will send RPC to MS to request changes to effective log
// level of all DAOS engines in a system.
func SetEngineLogMasks(ctx context.Context, rpcClient UnaryInvoker, req *SetEngineLogMasksReq) (*SetEngineLogMasksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.Masks != "" {
		if err := engine.ValidateLogMasks(req.Masks); err != nil {
			return nil, err
		}
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).SetEngineLogMasks(ctx, &ctlpb.SetLogMasksReq{Masks: req.Masks})
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SetEngineLogMasksResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := resp.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
		}
	}

	return resp, nil
}
