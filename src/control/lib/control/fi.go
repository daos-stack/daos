//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build fault_injection
// +build fault_injection

package control

import (
	"context"

	"google.golang.org/protobuf/proto"
)

type faultInjectionReq struct {
	msRequest
	unaryRequest
	retryableRequest
}

// InvokeFaultRPC is meant to be used during fault injection tests. It
// provides a bare-bones RPC client that can be used to invoke any RPC
// directly without translation between protobuf messages and native types.
func InvokeFaultRPC(ctx context.Context, rpcClient UnaryInvoker, rpc unaryRPC) (proto.Message, error) {
	req := new(faultInjectionReq)
	req.setRPC(rpc)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	return ur.getMSResponse()
}
