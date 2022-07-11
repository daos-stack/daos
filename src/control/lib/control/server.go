//
// (C) Copyright 2021-2022 Intel Corporation.
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
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/engine"
)

// SetEngineLogMasksReq contains the inputs for the set engine log level
// request.
type SetEngineLogMasksReq struct {
	unaryRequest
	Masks string `json:"masks"`
}

// SetEngineLogMasksResp contains the results of a set engine log level
// request.
type SetEngineLogMasksResp struct {
	HostErrorsResp
}

// SetEngineLogMasks will send RPC to hostlist to request changes to log
// level of all DAOS engines on each host in list.
func SetEngineLogMasks(ctx context.Context, rpcClient UnaryInvoker, req *SetEngineLogMasksReq) (*SetEngineLogMasksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.Masks != "" {
		if err := engine.ValidateLogMasks(req.Masks); err != nil {
			return nil, err
		}
	}

	pbReq := new(ctlpb.SetLogMasksReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, err
	}
	pbReq.Sys = req.getSystem(rpcClient)
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).SetEngineLogMasks(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS set engine log masks request: %+v", pbReq)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		rpcClient.Debugf("failed to invoke set engine log masks RPC: %s", err)
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

	rpcClient.Debugf("DAOS set engine log masks response: %+v", resp)
	return resp, nil
}
