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
			Svcreps: system.RanksToUint32(ps.Replicas),
		})
	}

	return
}

func (mod *srvModule) handleCheckerRegisterPool(_ context.Context, reqb []byte) (out []byte, outErr error) {
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

	uuid, err := uuid.Parse(req.Uuid)
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
	if _, err := mod.poolDB.FindPoolServiceByUUID(uuid); err == nil {
		mod.log.Errorf("pool with uuid %q already exists", req.Uuid)
		resp.Status = int32(daos.Exists)
		return
	}
	if _, err := mod.poolDB.FindPoolServiceByLabel(req.Label); err == nil {
		mod.log.Errorf("pool with label %q already exists", req.Label)
		resp.Status = int32(daos.Exists)
		return
	}

	ps := &system.PoolService{
		PoolUUID:  uuid,
		PoolLabel: req.Label,
		State:     system.PoolServiceStateReady,
		Replicas:  system.RanksFromUint32(req.Svcreps),
	}

	if err := mod.poolDB.AddPoolService(ps); err != nil {
		mod.log.Errorf("failed to register pool: %s", err)
		resp.Status = int32(daos.MiscError)
		return
	}

	return
}

func (mod *srvModule) handleCheckerDeregisterPool(_ context.Context, reqb []byte) (out []byte, outErr error) {
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

	uuid, err := uuid.Parse(req.Uuid)
	if err != nil {
		mod.log.Errorf("invalid pool UUID %q: %s", req.Uuid, err)
		resp.Status = int32(daos.InvalidInput)
		return
	}

	if _, err := mod.poolDB.FindPoolServiceByUUID(uuid); err != nil {
		if system.IsPoolNotFound(err) {
			mod.log.Errorf("pool with uuid %q does not exist", req.Uuid)
			resp.Status = int32(daos.Nonexistent)
		} else {
			mod.log.Errorf("failed to check pool uuid: %s", err)
			resp.Status = int32(daos.MiscError)
		}
		return
	}

	if err := mod.poolDB.RemovePoolService(uuid); err != nil {
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

	finding := checker.AnnotateFinding(checker.NewFinding(req.Report))
	if err := mod.checkerDB.AddCheckerFinding(finding); err != nil {
		mod.log.Errorf("failed to add checker finding %+v: %s", finding, err)
		resp.Status = int32(daos.MiscError)
		return
	}

	return
}
