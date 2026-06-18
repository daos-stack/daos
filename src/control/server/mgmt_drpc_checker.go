//
// (C) Copyright 2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/system"
)

func (mod *srvModule) handleCheckerListPools(_ context.Context, reqb []byte) (out []byte, outErr error) {
	// TODO: Remove if we never add request fields?
	req := new(sharedpb.CheckListPoolReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerListPools: %+v", req)

	resp := new(sharedpb.CheckListPoolResp)
	defer func() {
		mod.log.Debugf("CheckerListPools resp: %+v", resp)
		out, outErr = proto.Marshal(resp)
	}()

	pools, err := mod.poolDB.PoolServiceList(true)
	if err != nil {
		mod.log.Errorf("failed to list pools: %s", err)
		resp.Status = int32(daos.MiscError)
		return
	}

	for _, ps := range pools {
		resp.Pools = append(resp.Pools, &sharedpb.CheckListPoolResp_OnePool{
			Uuid:    ps.PoolUUID.String(),
			Label:   ps.PoolLabel,
			Svcreps: ranklist.RanksToUint32(ps.Replicas),
		})
	}

	return
}

func (mod *srvModule) handleCheckerRegisterPool(parent context.Context, reqb []byte) (out []byte, outErr error) {
	req := new(sharedpb.CheckRegPoolReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerRegisterPool: %+v", req)

	resp := new(sharedpb.CheckRegPoolResp)
	defer func() {
		mod.log.Debugf("CheckerRegisterPool resp: %+v", resp)
		out, outErr = proto.Marshal(resp)
	}()

	poolUUID, err := uuid.Parse(req.Uuid)
	if err != nil {
		mod.log.Errorf("invalid pool UUID %q: %s", req.Uuid, err)
		resp.Status = int32(daos.InvalidInput)
		return
	}
	if !daos.LabelIsValid(req.Label) {
		mod.log.Errorf("bad pool label %q", req.Label)
		resp.Status = int32(daos.InvalidInput)
		return
	}
	if len(req.Svcreps) == 0 {
		mod.log.Errorf("pool %q has zero svcreps", req.Uuid)
		resp.Status = int32(daos.InvalidInput)
		return
	}

	lock, err := mod.poolDB.TakePoolLock(parent, poolUUID)
	if err != nil {
		mod.log.Errorf("failed to take pool lock: %s", err)
		resp.Status = int32(daos.MiscError)
		return
	}
	defer lock.Release()
	ctx := lock.InContext(parent)

	ps, err := mod.poolDB.FindPoolServiceByUUID(poolUUID)
	if err == nil {
		// We're updating an existing pool service.
		if ps.PoolLabel != req.Label {
			if _, err := mod.poolDB.FindPoolServiceByLabel(req.Label); err == nil {
				mod.log.Errorf("pool with label %q already exists", req.Label)
				resp.Status = int32(daos.Exists)
				return
			}
		}
		ps.PoolLabel = req.Label
		ps.Replicas = ranklist.RanksFromUint32(req.Svcreps)

		mod.log.Debugf("updating pool service from req: %+v", req)
		if err := mod.poolDB.UpdatePoolService(ctx, ps); err != nil {
			mod.log.Errorf("failed to update pool: %s", err)
			resp.Status = int32(daos.MiscError)
			return
		}

		return
	} else if !system.IsPoolNotFound(err) {
		mod.log.Errorf("failed to find pool: %s", err)
		resp.Status = int32(daos.MiscError)
		return
	}

	if _, err := mod.poolDB.FindPoolServiceByLabel(req.Label); err == nil {
		mod.log.Errorf("pool with label %q already exists", req.Label)
		resp.Status = int32(daos.Exists)
		return
	}

	ps = &system.PoolService{
		PoolUUID:  poolUUID,
		PoolLabel: req.Label,
		State:     system.PoolServiceStateReady,
		Replicas:  ranklist.RanksFromUint32(req.Svcreps),
	}

	mod.log.Debugf("adding pool service from req: %+v", req)
	if err := mod.poolDB.AddPoolService(ctx, ps); err != nil {
		mod.log.Errorf("failed to register pool: %s", err)
		resp.Status = int32(daos.MiscError)
		return
	}

	return
}

func (mod *srvModule) handleCheckerDeregisterPool(parent context.Context, reqb []byte) (out []byte, outErr error) {
	req := new(sharedpb.CheckDeregPoolReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerDeregisterPool: %+v", req)

	resp := new(sharedpb.CheckDeregPoolResp)
	defer func() {
		mod.log.Debugf("CheckerDeregisterPool resp: %+v", resp)
		out, outErr = proto.Marshal(resp)
	}()

	poolUUID, err := uuid.Parse(req.Uuid)
	if err != nil {
		mod.log.Errorf("invalid pool UUID %q: %s", req.Uuid, err)
		resp.Status = int32(daos.InvalidInput)
		return
	}

	lock, err := mod.poolDB.TakePoolLock(parent, poolUUID)
	if err != nil {
		mod.log.Errorf("failed to take pool lock: %s", err)
		resp.Status = int32(daos.MiscError)
		return
	}
	defer lock.Release()
	ctx := lock.InContext(parent)

	if _, err := mod.poolDB.FindPoolServiceByUUID(poolUUID); err != nil {
		if system.IsPoolNotFound(err) {
			mod.log.Errorf("pool with uuid %q does not exist", req.Uuid)
			resp.Status = int32(daos.Nonexistent)
		} else {
			mod.log.Errorf("failed to check pool uuid: %s", err)
			resp.Status = int32(daos.MiscError)
		}
		return
	}

	if err := mod.poolDB.RemovePoolService(ctx, poolUUID); err != nil {
		mod.log.Errorf("failed to remove pool: %s", err)
		resp.Status = int32(daos.MiscError)
		return
	}

	return
}

func chkReportErrToDaosStatus(err error) daos.Status {
	err = errors.Cause(err) // Fully unwrap before attempting comparisons

	if status, ok := err.(daos.Status); ok {
		return status
	}

	switch {
	case control.IsRetryableConnErr(err), system.IsNotLeader(err), system.IsNotReplica(err):
		// If these errors manage to boil to the top, it's probably worth trying over.
		return daos.TryAgain
	case control.IsConnErr(err), control.IsMSConnectionFailure(err):
		return daos.Unreachable
	case fault.IsFaultCode(err, code.SystemCheckerNotEnabled),
		fault.IsFaultCode(err, code.SystemCheckerInvalidMemberStates):
		return daos.NotApplicable
	default:
		return daos.MiscError
	}
}

func (mod *srvModule) handleCheckerReport(ctx context.Context, reqb []byte) (out []byte, outErr error) {
	req := new(sharedpb.CheckReportReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerReport: %+v", req)

	// Forward the report to the MS
	msReq := &control.SystemCheckEngineReportReq{
		CheckReportReq: *req,
	}
	msReq.SetHostList(mod.msReplicas)

	mod.log.Debugf("forwarding check report to MS: %+v", msReq)
	resp, err := control.SystemCheckEngineReport(ctx, mod.rpcClient, msReq)

	var drpcResp *sharedpb.CheckReportResp
	if err != nil {
		mod.log.Errorf("SystemCheckEngineReport failed: %s", err)
		drpcResp = &sharedpb.CheckReportResp{
			Status: chkReportErrToDaosStatus(err).Int32(),
		}
	} else {
		drpcResp = &resp.CheckReportResp
	}

	return proto.Marshal(drpcResp)
}
