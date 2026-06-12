//
// (C) Copyright 2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"

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

func (mod *srvModule) handleCheckerRegisterPool(ctx context.Context, reqb []byte) (out []byte, outErr error) {
	req := new(sharedpb.CheckRegPoolReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerRegisterPool: %+v", req)

	// Forward the report to the MS
	msReq := &control.SystemCheckRegPoolReq{
		CheckRegPoolReq: *req,
	}
	msReq.SetHostList(mod.msReplicas)

	mod.log.Debugf("forwarding check register pool req to MS: %+v", msReq)
	resp, err := control.SystemCheckRegPool(ctx, mod.rpcClient, msReq)

	var drpcResp *sharedpb.CheckRegPoolResp
	if err != nil {
		mod.log.Errorf("SystemCheckRegPool failed: %s", err)
		drpcResp = &sharedpb.CheckRegPoolResp{
			Status: grpcErrToDaosStatus(err).Int32(),
		}
	} else {
		drpcResp = &resp.CheckRegPoolResp
	}

	mod.log.Debugf("CheckerRegPool resp: %+v", drpcResp)
	return proto.Marshal(drpcResp)
}

func (mod *srvModule) handleCheckerDeregisterPool(ctx context.Context, reqb []byte) (out []byte, outErr error) {
	req := new(sharedpb.CheckDeregPoolReq)
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	mod.log.Debugf("handling CheckerDeregisterPool: %+v", req)

	// Forward the report to the MS
	msReq := &control.SystemCheckDeregPoolReq{
		CheckDeregPoolReq: *req,
	}
	msReq.SetHostList(mod.msReplicas)

	mod.log.Debugf("forwarding check deregister pool req to MS: %+v", msReq)
	resp, err := control.SystemCheckDeregPool(ctx, mod.rpcClient, msReq)

	var drpcResp *sharedpb.CheckDeregPoolResp
	if err != nil {
		mod.log.Errorf("SystemCheckDeregPool failed: %s", err)
		drpcResp = &sharedpb.CheckDeregPoolResp{
			Status: grpcErrToDaosStatus(err).Int32(),
		}
	} else {
		drpcResp = &resp.CheckDeregPoolResp
	}

	mod.log.Debugf("CheckerDeregPool resp: %+v", drpcResp)
	return proto.Marshal(drpcResp)
}

func grpcErrToDaosStatus(err error) daos.Status {
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
		// The dreaded DER_MISC error. Who knows what happened? We don't!
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
			Status: grpcErrToDaosStatus(err).Int32(),
		}
	} else {
		drpcResp = &resp.CheckReportResp
	}

	return proto.Marshal(drpcResp)
}
