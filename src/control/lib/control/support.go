//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
)

type (
	// CollectLogReq contains the parameters for a collectlog request.
	CollectLogReq struct {
		unaryRequest
		TargetFolder string
		CustomLogs   string
		JsonOutput   bool
		LogFunction  int32
		LogCmd       string
	}

	// CollectLogResp contains the results of a collectlog
	CollectLogResp struct {
		HostErrorsResp
	}
)

// CollectLog concurrently performs log collection across all hosts
// supplied in the request's hostlist, or all configured hosts if not
// explicitly specified. The function blocks until all results (successful
// or otherwise) are received, and returns a single response structure
// containing results for all host log collection operations.
func CollectLog(ctx context.Context, rpcClient UnaryInvoker, req *CollectLogReq) (*CollectLogResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).CollectLog(ctx, &ctlpb.CollectLogReq{
			TargetFolder: req.TargetFolder,
			CustomLogs:   req.CustomLogs,
			JsonOutput:   req.JsonOutput,
			LogFunction:  req.LogFunction,
			LogCmd:       req.LogCmd,
		})
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	scr := new(CollectLogResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := scr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

	}

	return scr, nil
}
