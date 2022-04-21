//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/system"
)

func (svc *mgmtSvc) makeCheckerCall(ctx context.Context, method drpc.Method, req proto.Message) (*drpc.Response, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	if err := svc.checkMemberStates(
		system.MemberStateAdminExcluded,
		system.MemberStateCheckerStarted,
	); err != nil {
		return nil, err
	}

	return svc.harness.CallDrpc(ctx, method, req)
}

type poolCheckerReq interface {
	proto.Message
	GetUuids() []string
}

func (svc *mgmtSvc) makePoolCheckerCall(ctx context.Context, method drpc.Method, req poolCheckerReq) (*drpc.Response, error) {
	poolUuids := make([]string, len(req.GetUuids()))
	for i, id := range req.GetUuids() {
		uuid, err := svc.resolvePoolID(id)
		if err != nil {
			return nil, err
		}
		poolUuids[i] = uuid.String()
	}

	switch r := req.(type) {
	case *mgmtpb.CheckStartReq:
		checkRanks, err := svc.sysdb.MemberRanks(system.MemberStateCheckerStarted)
		if err != nil {
			return nil, err
		}

		r.Ranks = system.RanksToUint32(checkRanks)
		r.Uuids = poolUuids
	case *mgmtpb.CheckStopReq:
		r.Uuids = poolUuids
	case *mgmtpb.CheckQueryReq:
		r.Uuids = poolUuids
	default:
		return nil, errors.Errorf("unexpected request type %T", req)
	}

	return svc.makeCheckerCall(ctx, method, req)
}

func (svc *mgmtSvc) SystemCheckStart(ctx context.Context, req *mgmtpb.CheckStartReq) (resp *mgmtpb.CheckStartResp, err error) {
	defer func() {
		svc.log.Debugf("Responding to SystemCheckStart RPC: %s (%+v)", mgmtpb.Debug(resp), err)
	}()
	svc.log.Debugf("Received SystemCheckStart RPC: %+v", req)

	dResp, err := svc.makePoolCheckerCall(ctx, drpc.MethodCheckerStart, req)
	if err != nil {
		return nil, err
	}

	resp = new(mgmtpb.CheckStartResp)
	if err = proto.Unmarshal(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckStart response")
	}

	return resp, nil
}

func (svc *mgmtSvc) SystemCheckStop(ctx context.Context, req *mgmtpb.CheckStopReq) (resp *mgmtpb.CheckStopResp, err error) {
	defer func() {
		svc.log.Debugf("Responding to SystemCheckStop RPC: %s (%+v)", mgmtpb.Debug(resp), err)
	}()
	svc.log.Debugf("Received SystemCheckStop RPC: %+v", req)

	dResp, err := svc.makePoolCheckerCall(ctx, drpc.MethodCheckerStop, req)
	if err != nil {
		return nil, err
	}

	resp = new(mgmtpb.CheckStopResp)
	if err = proto.Unmarshal(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckStop response")
	}

	return resp, nil
}

func (svc *mgmtSvc) SystemCheckQuery(ctx context.Context, req *mgmtpb.CheckQueryReq) (resp *mgmtpb.CheckQueryResp, err error) {
	defer func() {
		svc.log.Debugf("Responding to SystemCheckQuery RPC: %s (%+v)", mgmtpb.Debug(resp), err)
	}()
	svc.log.Debugf("Received SystemCheckQuery RPC: %+v", req)

	dResp, err := svc.makePoolCheckerCall(ctx, drpc.MethodCheckerQuery, req)
	if err != nil {
		return nil, err
	}

	resp = new(mgmtpb.CheckQueryResp)
	if err = proto.Unmarshal(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckQuery response")
	}

	cfList, err := svc.sysdb.GetCheckerFindings()
	if err != nil {
		return nil, err
	}

	for _, f := range cfList {
		resp.Reports = append(resp.Reports, &f.CheckReport)
	}

	return resp, nil
}

func (svc *mgmtSvc) SystemCheckProp(ctx context.Context, req *mgmtpb.CheckPropReq) (resp *mgmtpb.CheckPropResp, err error) {
	defer func() {
		svc.log.Debugf("Responding to SystemCheckProp RPC: %s (%+v)", mgmtpb.Debug(resp), err)
	}()
	svc.log.Debugf("Received SystemCheckProp RPC: %+v", req)

	dResp, err := svc.makeCheckerCall(ctx, drpc.MethodCheckerProp, req)
	if err != nil {
		return nil, err
	}

	resp = new(mgmtpb.CheckPropResp)
	if err = proto.Unmarshal(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckProp response")
	}

	return resp, nil
}

func (svc *mgmtSvc) SystemCheckRepair(ctx context.Context, req *mgmtpb.CheckActReq) (resp *mgmtpb.CheckActResp, err error) {
	defer func() {
		svc.log.Debugf("Responding to SystemCheckRepair RPC: %s (%+v)", mgmtpb.Debug(resp), err)
	}()
	svc.log.Debugf("Received SystemCheckRepair RPC: %+v", req)

	f, err := svc.sysdb.GetCheckerFinding(req.Seq)
	if err != nil {
		return nil, err
	}

	if !f.HasChoice(req.Act) {
		return nil, errors.Errorf("invalid action %d (must be one of %s)", req.Act, f.ValidChoicesString())
	}

	dResp, err := svc.makeCheckerCall(ctx, drpc.MethodCheckerAction, req)
	if err != nil {
		return nil, err
	}

	if err := svc.sysdb.SetCheckerFindingAction(req.Seq, int32(req.Act)); err != nil {
		return nil, err
	}

	resp = new(mgmtpb.CheckActResp)
	if err = proto.Unmarshal(dResp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal CheckRepair response")
	}

	return resp, nil
}
