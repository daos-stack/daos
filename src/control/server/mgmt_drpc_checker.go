//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"

	"github.com/google/uuid"
	"google.golang.org/protobuf/proto"

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
)

func (mod *srvModule) handleCheckerListPools(_ context.Context, reqb []byte) (out []byte, outErr error) {
	// TODO: Remove if we never add request fields?
	req := new(srvpb.CheckListPoolReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerListPools: %+v", req)

	resp := new(srvpb.CheckListPoolResp)
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
		resp.Pools = append(resp.Pools, &srvpb.CheckListPoolResp_OnePool{
			Uuid:    ps.PoolUUID.String(),
			Label:   ps.PoolLabel,
			Svcreps: ranklist.RanksToUint32(ps.Replicas),
		})
	}

	return
}

func (mod *srvModule) handleCheckerRegisterPool(parent context.Context, reqb []byte) (out []byte, outErr error) {
	req := new(srvpb.CheckRegPoolReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerRegisterPool: %+v", req)

	resp := new(srvpb.CheckRegPoolResp)
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
	req := new(srvpb.CheckDeregPoolReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerDeregisterPool: %+v", req)

	resp := new(srvpb.CheckDeregPoolResp)
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

func (mod *srvModule) handleCheckerReport(_ context.Context, reqb []byte) (out []byte, outErr error) {
	req := new(srvpb.CheckReportReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerReport: %+v", req)

	resp := new(srvpb.CheckReportResp)
	defer func() {
		mod.log.Debugf("CheckerReport resp: %+v", resp)
		out, outErr = proto.Marshal(resp)
	}()

	if req.Report != nil && req.Report.PoolLabel == "" && req.Report.PoolUuid != "" {
		poolUUID, err := uuid.Parse(req.Report.PoolUuid)
		if err != nil {
			mod.log.Errorf("invalid pool UUID %q: %s", req.Report.PoolUuid, err)
			resp.Status = int32(daos.InvalidInput)
			return
		}

		if ps, err := mod.poolDB.FindPoolServiceByUUID(poolUUID); err == nil {
			// Annotate the report with the pool label for the user.
			// NB: In some cases this label may be incorrect, in which
			// case the user will want to use the verbose or JSON output
			// modes of the checker in order to get the UUID.
			req.Report.PoolLabel = ps.PoolLabel
		}
	}

	finding := checker.AnnotateFinding(checker.NewFinding(req.Report))
	mod.log.Debugf("annotated finding: %+v", finding)
	if err := mod.checkerDB.AddOrUpdateCheckerFinding(finding); err != nil {
		mod.log.Errorf("failed to add checker finding %+v: %s", finding, err)
		resp.Status = int32(daos.MiscError)
		return
	}

	return
}
