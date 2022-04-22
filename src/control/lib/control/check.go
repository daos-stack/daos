//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

const (
	SystemCheckFlagDryRun  = uint32(chkpb.CheckFlag_CF_DRYRUN)
	SystemCheckFlagReset   = uint32(chkpb.CheckFlag_CF_RESET)
	SystemCheckFlagFailout = uint32(chkpb.CheckFlag_CF_FAILOUT)
	SystemCheckFlagAuto    = uint32(chkpb.CheckFlag_CF_AUTO)
)

type SystemCheckStartReq struct {
	unaryRequest
	msRequest

	mgmtpb.CheckStartReq
}

// SystemCheckStart starts the system checker.
func SystemCheckStart(ctx context.Context, rpcClient UnaryInvoker, req *SystemCheckStartReq) error {
	if req == nil {
		return errors.Errorf("nil %T", req)
	}

	req.CheckStartReq.Sys = req.getSystem(rpcClient)
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemCheckStart(ctx, &req.CheckStartReq)
	})
	rpcClient.Debugf("DAOS system check start request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}
	ms, err := ur.getMSResponse()
	if err != nil {
		return err
	}
	rpcClient.Debugf("DAOS system check start response: %+v", ms)

	return nil
}

type SystemCheckStopReq struct {
	unaryRequest
	msRequest

	mgmtpb.CheckStopReq
}

// SystemCheckStop stops the system checker.
func SystemCheckStop(ctx context.Context, rpcClient UnaryInvoker, req *SystemCheckStopReq) error {
	if req == nil {
		return errors.Errorf("nil %T", req)
	}

	req.CheckStopReq.Sys = req.getSystem(rpcClient)
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemCheckStop(ctx, &req.CheckStopReq)
	})
	rpcClient.Debugf("DAOS system check stop request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}
	ms, err := ur.getMSResponse()
	if err != nil {
		return err
	}
	rpcClient.Debugf("DAOS system check stop response: %+v", ms)

	return nil
}

type SystemCheckQueryReq struct {
	unaryRequest
	msRequest

	mgmtpb.CheckQueryReq
}

type SystemCheckQueryResp struct {
	mgmtpb.CheckQueryResp
}

// SystemCheckQuery queries the system checker status.
func SystemCheckQuery(ctx context.Context, rpcClient UnaryInvoker, req *SystemCheckQueryReq) (*SystemCheckQueryResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T", req)
	}

	req.CheckQueryReq.Sys = req.getSystem(rpcClient)
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemCheckQuery(ctx, &req.CheckQueryReq)
	})
	rpcClient.Debugf("DAOS system check query request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}
	ms, err := ur.getMSResponse()
	if err != nil {
		return nil, err
	}
	rpcClient.Debugf("DAOS system check query response: %+v", ms)

	resp := new(SystemCheckQueryResp)
	if pbResp, ok := ms.(*mgmtpb.CheckQueryResp); ok {
		resp.CheckQueryResp = *pbResp
	} else {
		return nil, errors.Errorf("unexpected response type %T", ms)
	}
	return resp, nil
}

type SystemCheckPropReq struct {
	unaryRequest
	msRequest

	mgmtpb.CheckPropReq
}

type SystemCheckPropResp struct {
	pb *mgmtpb.CheckPropResp
}

func (r *SystemCheckPropResp) MarshalJSON() ([]byte, error) {
	return json.Marshal(r.pb)
}

// SystemCheckProp queries the system checker properties.
func SystemCheckProp(ctx context.Context, rpcClient UnaryInvoker, req *SystemCheckPropReq) (*SystemCheckPropResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T", req)
	}

	req.CheckPropReq.Sys = req.getSystem(rpcClient)
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemCheckProp(ctx, &req.CheckPropReq)
	})
	rpcClient.Debugf("DAOS system check prop request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}
	ms, err := ur.getMSResponse()
	if err != nil {
		return nil, err
	}
	rpcClient.Debugf("DAOS system check prop response: %+v", ms)

	resp := new(SystemCheckPropResp)
	if pbResp, ok := ms.(*mgmtpb.CheckPropResp); ok {
		resp.pb = pbResp
	} else {
		return nil, errors.Errorf("unexpected response type %T", ms)
	}
	return resp, nil
}

type SystemCheckRepairReq struct {
	unaryRequest
	msRequest

	mgmtpb.CheckActReq
}

func (r *SystemCheckRepairReq) SetAction(action int32) error {
	if _, ok := chkpb.CheckInconsistAction_name[action]; !ok {
		return errors.Errorf("invalid action %d", action)
	}
	r.Act = chkpb.CheckInconsistAction(action)
	return nil
}

// SystemCheckRepair sends a request to the system checker to indicate
// what the desired repair action is for a reported inconsistency.
func SystemCheckRepair(ctx context.Context, rpcClient UnaryInvoker, req *SystemCheckRepairReq) error {
	if req == nil {
		return errors.Errorf("nil %T", req)
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		req.CheckActReq.Sys = req.getSystem(rpcClient)
		return mgmtpb.NewMgmtSvcClient(conn).SystemCheckRepair(ctx, &req.CheckActReq)
	})
	rpcClient.Debugf("DAOS system check repair request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}
	msResp, err := ur.getMSResponse()
	if err != nil {
		return err
	}
	rpcClient.Debugf("DAOS system check repair response: %+v", msResp)

	return nil
}
