//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build fault_injection
// +build fault_injection

package server

import (
	"context"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
)

func (svc *mgmtSvc) FaultInjectReport(ctx context.Context, rpt *chkpb.CheckReport) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(rpt); err != nil {
		return nil, err
	}

	cf := checker.NewFinding(rpt)
	if err := svc.sysdb.AddCheckerFinding(cf); err != nil {
		return nil, err
	}

	return new(mgmtpb.DaosResp), nil
}

func (svc *mgmtSvc) FaultInjectMgmtPoolFault(parent context.Context, fault *chkpb.Fault) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(fault); err != nil {
		return nil, err
	}

	var poolID string
	var newLabel string
	switch len(fault.Strings) {
	case 1:
		poolID = fault.Strings[0]
	case 2:
		poolID = fault.Strings[0]
		newLabel = fault.Strings[1]
	default:
		return nil, errors.New("no pool UUID provided")
	}

	ps, err := svc.getPoolService(poolID)
	if err != nil {
		return nil, err
	}
	if newLabel == "" {
		newLabel = ps.PoolLabel + "-fault"
	}

	lock, err := svc.sysdb.TakePoolLock(parent, ps.PoolUUID)
	if err != nil {
		return nil, err
	}
	defer lock.Release()
	ctx := lock.InContext(parent)

	var newRanks []ranklist.Rank
	switch len(fault.Uints) {
	case 0:
		if len(ps.Replicas) == 0 {
			newRanks = []ranklist.Rank{0, 3, 6, 9}
		} else {
			newRanks = []ranklist.Rank{ps.Replicas[0]}
		}
	default:
		newRanks = ranklist.RanksFromUint32(fault.Uints)
	}

	switch fault.Class {
	case chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL:
		ps.PoolLabel = newLabel
	case chkpb.CheckInconsistClass_CIC_POOL_BAD_SVCL:
		ps.Replicas = newRanks
	case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS:
		if err := svc.sysdb.RemovePoolService(ctx, ps.PoolUUID); err != nil {
			return nil, err
		}
		ps = nil
	default:
		return nil, errors.Errorf("unhandled fault class %q", fault.Class)
	}

	if ps != nil {
		if err := svc.sysdb.UpdatePoolService(ctx, ps); err != nil {
			return nil, err
		}
	}
	return new(mgmtpb.DaosResp), nil
}

func (svc *mgmtSvc) FaultInjectPoolFault(parent context.Context, fault *chkpb.Fault) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(fault); err != nil {
		return nil, err
	}

	var poolID string
	var newLabel string
	switch len(fault.Strings) {
	case 1:
		poolID = fault.Strings[0]
	case 2:
		poolID = fault.Strings[0]
		newLabel = fault.Strings[1]
	default:
		return nil, errors.New("no pool UUID provided")
	}

	ps, err := svc.getPoolService(poolID)
	if err != nil {
		return nil, err
	}
	if newLabel == "" {
		newLabel = ps.PoolLabel + "-fault"
	}

	lock, err := svc.sysdb.TakePoolLock(parent, ps.PoolUUID)
	if err != nil {
		return nil, err
	}
	defer lock.Release()
	ctx := lock.InContext(parent)

	resp := new(mgmtpb.DaosResp)
	switch fault.Class {
	case chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL:
		prop := &mgmtpb.PoolProperty{
			Number: uint32(daos.PoolPropertyLabel),
			Value:  &mgmtpb.PoolProperty_Strval{Strval: newLabel},
		}
		req := &mgmtpb.PoolSetPropReq{
			Id:         poolID,
			Properties: []*mgmtpb.PoolProperty{prop},
		}

		var dresp *drpc.Response
		dresp, err = svc.makePoolServiceCall(ctx, drpc.MethodPoolSetProp, req)
		if err != nil {
			return nil, err
		}

		if err = proto.Unmarshal(dresp.Body, resp); err != nil {
			return nil, errors.Wrap(err, "unmarshal PoolSetProp response")
		}

		if resp.GetStatus() != 0 {
			return nil, errors.Errorf("label update failed: %s", drpc.Status(resp.Status))
		}
	case chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE:
		allRanks, err := svc.sysdb.MemberRanks(system.NonExcludedMemberFilter)
		if err != nil {
			return nil, err
		}
		req := &mgmtpb.PoolDestroyReq{
			Id:       ps.PoolUUID.String(),
			SvcRanks: ranklist.RanksToUint32(allRanks),
			Force:    true,
		}
		dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodPoolDestroy, req)
		if err != nil {
			return nil, err
		}

		if err = proto.Unmarshal(dresp.Body, resp); err != nil {
			return nil, errors.Wrap(err, "unmarshal PoolDestroy response")
		}

		if resp.GetStatus() != 0 {
			return nil, errors.Errorf("pool destroy failed: %s", drpc.Status(resp.Status))
		}
	default:
		return nil, errors.Errorf("unhandled fault class %q", fault.Class)
	}

	return resp, nil
}
