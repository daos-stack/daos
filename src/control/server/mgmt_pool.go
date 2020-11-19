//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package server

import (
	"strings"

	"github.com/golang/protobuf/proto"
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

type poolServiceReq interface {
	proto.Message
	GetUuid() string
	GetSvcRanks() []uint32
	SetSvcRanks(rl []uint32)
}

func (svc *mgmtSvc) makePoolServiceCall(ctx context.Context, method drpc.Method, req poolServiceReq) (*drpc.Response, error) {
	if len(req.GetSvcRanks()) == 0 {
		rl, err := svc.getPoolServiceRanks(req.GetUuid())
		if err != nil {
			return nil, err
		}
		req.SetSvcRanks(rl)
	}

	return svc.harness.CallDrpc(ctx, method, req)
}

func (svc *mgmtSvc) getPoolServiceRanks(uuidStr string) ([]uint32, error) {
	uuid, err := uuid.Parse(uuidStr)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse request uuid %q", uuidStr)
	}

	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		return nil, err
	}

	if ps.State != system.PoolServiceStateReady {
		return nil, drpc.DaosTryAgain
	}

	return system.RanksToUint32(ps.Replicas), nil
}

// calculateCreateStorage determines the amount of SCM/NVMe storage to
// allocate per server in order to fulfill the create request, if those
// values are not already supplied as part of the request.
func (svc *mgmtSvc) calculateCreateStorage(req *mgmtpb.PoolCreateReq) error {
	instances := svc.harness.Instances()
	if len(instances) < 1 {
		return errors.New("harness has no managed instances")
	}

	targetCount := instances[0].GetTargetCount()
	if targetCount == 0 {
		return errors.New("zero target count")
	}
	if req.Scmbytes < ioserver.ScmMinBytesPerTarget*uint64(targetCount) {
		return FaultPoolScmTooSmall(req.Scmbytes, targetCount)
	}
	if req.Nvmebytes != 0 && req.Nvmebytes < ioserver.NvmeMinBytesPerTarget*uint64(targetCount) {
		return FaultPoolNvmeTooSmall(req.Nvmebytes, targetCount)
	}

	return nil
}

// PoolCreate implements the method defined for the Management Service.
//
// Validate minimum SCM/NVMe pool size per VOS target, pool size request params
// are per-ioserver so need to be larger than (minimum_target_allocation *
// target_count).
func (svc *mgmtSvc) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq) (resp *mgmtpb.PoolCreateResp, err error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	resp = new(mgmtpb.PoolCreateResp)

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch, req:%+v\n", req)

	if err := svc.calculateCreateStorage(req); err != nil {
		return nil, err
	}

	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse pool UUID %q", req.GetUuid())
	}

	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if ps != nil {
		svc.log.Debugf("found pool %s state=%s", ps.PoolUUID, ps.State)
		resp.Status = int32(drpc.DaosAlready)
		if ps.State == system.PoolServiceStateCreating {
			resp.Status = int32(drpc.DaosTryAgain)
		}
		return resp, nil
	}
	if _, ok := err.(*system.ErrPoolNotFound); !ok {
		return nil, err
	}

	allRanks, err := svc.sysdb.MemberRanks(system.AvailableMemberFilter)
	if err != nil {
		return nil, err
	}

	if len(req.GetRanks()) > 0 {
		// If the request supplies a specific rank list, use it. Note that
		// the rank list may include downed ranks, in which case the create
		// will fail with an error.
		reqRanks := system.RanksFromUint32(req.GetRanks())
		// Create a RankSet to sort/dedupe the ranks.
		reqRanks = system.RankSetFromRanks(reqRanks).Ranks()
		if err != nil {
			return nil, err
		}

		if invalid := system.CheckRankMembership(allRanks, reqRanks); len(invalid) > 0 {
			return nil, FaultPoolInvalidRanks(invalid)
		}

		req.Ranks = system.RanksToUint32(reqRanks)
	} else {
		// Otherwise, create the pool across all available ranks in the system.
		req.Ranks = system.RanksToUint32(allRanks)
	}

	ps = &system.PoolService{
		PoolUUID: uuid,
		State:    system.PoolServiceStateCreating,
	}

	if err := svc.sysdb.AddPoolService(ps); err != nil {
		return nil, err
	}

	defer func() {
		var cuErr error
		switch {
		// No pool service created; nothing to clean up
		case ps == nil:
			return
		// No error and pool create went OK, nothing to do
		case err == nil && resp.GetStatus() == 0:
			return
		// Error after pool was created
		case err != nil && resp.GetStatus() == 0:
			svc.log.Errorf("cleaning up pool %s due to create failure: %q", req.Uuid, err)

			var pdResp *mgmtpb.PoolDestroyResp
			pdResp, cuErr = svc.PoolDestroy(ctx,
				&mgmtpb.PoolDestroyReq{
					Uuid:     req.Uuid,
					Sys:      req.Sys,
					Force:    true,
					SvcRanks: req.Ranks,
				})
			if cuErr != nil {
				svc.log.Errorf("error while destroying pool %s: %s", req.Uuid, cuErr)
				break
			}
			if pdResp.GetStatus() != 0 {
				cuErr = errors.Errorf("failed to destroy pool %s: %s",
					req.Uuid, drpc.DaosStatus(pdResp.GetStatus()))
			}
		}

		if cuErr == nil {
			svc.log.Errorf("removed pool service entry for %s in cleanup", req.Uuid)
			return
		}
	}()

	dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodPoolCreate, req)
	if err != nil {
		return nil, err
	}

	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolCreate response")
	}

	// let the caller know how many ranks were used
	resp.Numranks = int32(len(req.Ranks))

	if resp.GetStatus() != 0 {
		if err := svc.sysdb.RemovePoolService(ps.PoolUUID); err != nil {
			return nil, err
		}
		return resp, nil
	}

	ps.Replicas = system.RanksFromUint32(resp.GetSvcreps())
	ps.State = system.PoolServiceStateReady
	if err := svc.sysdb.UpdatePoolService(ps); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch resp:%+v\n", resp)

	return resp, nil
}

// PoolDestroy implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDestroy(ctx context.Context, req *mgmtpb.PoolDestroyReq) (*mgmtpb.PoolDestroyResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, req:%+v\n", req)

	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return nil, err
	}

	// FIXME: There are some potential races here. We may want
	// to somehow do all of this under a lock, to prevent multiple
	// gRPC callers from modifying the same pool concurrently.

	ps, err := svc.sysdb.FindPoolServiceByUUID(uuid)
	if err != nil {
		return nil, err
	}

	lastState := ps.State
	if ps.State == system.PoolServiceStateDestroying {
		return nil, drpc.DaosAlready
	}

	ps.State = system.PoolServiceStateDestroying
	if err := svc.sysdb.UpdatePoolService(ps); err != nil {
		return nil, err
	}

	req.SvcRanks = system.RanksToUint32(ps.Replicas)
	dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodPoolDestroy, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolDestroyResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDestroy response")
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, resp:%+v\n", resp)

	switch drpc.DaosStatus(resp.Status) {
	case drpc.DaosSuccess:
		if err := svc.sysdb.RemovePoolService(uuid); err != nil {
			return nil, errors.Wrapf(err, "failed to remove pool %s", uuid)
		}
	// TODO: Identify errors that should leave the pool in a "Destroying"
	// state and enumerate them here.
	default:
		// Revert the pool back to the previous state if the destroy failed
		// and the pool should not remain in the "Destroying" state.
		ps.State = lastState
		if err := svc.sysdb.UpdatePoolService(ps); err != nil {
			return nil, err
		}
	}

	return resp, nil
}

// PoolEvict implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolEvict(ctx context.Context, req *mgmtpb.PoolEvictReq) (*mgmtpb.PoolEvictResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PoolEvict dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolEvict, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolEvictResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolEvict response")
	}

	svc.log.Debugf("MgmtSvc.PoolEvict dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolExclude implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolExclude(ctx context.Context, req *mgmtpb.PoolExcludeReq) (*mgmtpb.PoolExcludeResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PoolExclude dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolExclude, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolExcludeResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolExclude response")
	}

	svc.log.Debugf("MgmtSvc.PoolExclude dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolDrain implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDrain(ctx context.Context, req *mgmtpb.PoolDrainReq) (*mgmtpb.PoolDrainResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PoolDrain dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolDrain, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolDrainResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDrain response")
	}

	svc.log.Debugf("MgmtSvc.PoolDrain dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolExtend implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolExtend(ctx context.Context, req *mgmtpb.PoolExtendReq) (*mgmtpb.PoolExtendResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PoolExtend dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolExtend, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolExtendResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolExtend response")
	}

	svc.log.Debugf("MgmtSvc.PoolExtend dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolReintegrate implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolReintegrate(ctx context.Context, req *mgmtpb.PoolReintegrateReq) (*mgmtpb.PoolReintegrateResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PoolReintegrate dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolReintegrate, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolReintegrateResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolReintegrate response")
	}

	svc.log.Debugf("MgmtSvc.PoolReintegrate dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolQuery forwards a pool query request to the I/O server.
func (svc *mgmtSvc) PoolQuery(ctx context.Context, req *mgmtpb.PoolQueryReq) (*mgmtpb.PoolQueryResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PoolQuery dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolQuery, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolQueryResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolQuery response")
	}

	svc.log.Debugf("MgmtSvc.PoolQuery dispatch, resp:%+v\n", resp)

	return resp, nil
}

// resolvePoolPropVal resolves string-based property names and values to their C equivalents.
func resolvePoolPropVal(req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropReq, error) {
	newReq := &mgmtpb.PoolSetPropReq{
		Uuid: req.Uuid,
	}

	propName := strings.TrimSpace(req.GetName())
	switch strings.ToLower(propName) {
	case "reclaim":
		newReq.SetPropertyNumber(drpc.PoolPropertySpaceReclaim)

		recType := strings.TrimSpace(req.GetStrval())
		switch strings.ToLower(recType) {
		case "disabled":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimDisabled)
		case "lazy":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimLazy)
		case "time":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimTime)
		default:
			return nil, errors.Errorf("unhandled reclaim type %q", recType)
		}
	// label not supported yet
	/*case "label":
	newReq.SetPropertyNumber(drpc.PoolPropertyLabel)
	newReq.SetValueString(req.GetStrval())*/
	case "space_rb":
		newReq.SetPropertyNumber(drpc.PoolPropertyReservedSpace)

		if strVal := req.GetStrval(); strVal != "" {
			return nil, errors.Errorf("invalid space_rb value %q (valid values: 0-100)", strVal)
		}

		rsPct := req.GetNumval()
		if rsPct > 100 {
			return nil, errors.Errorf("invalid space_rb value %d (valid values: 0-100)", rsPct)
		}
		newReq.SetValueNumber(rsPct)
	case "self_heal":
		newReq.SetPropertyNumber(drpc.PoolPropertySelfHealing)

		healType := strings.TrimSpace(req.GetStrval())
		switch strings.ToLower(healType) {
		case "exclude":
			newReq.SetValueNumber(drpc.PoolSelfHealingAutoExclude)
		case "rebuild":
			newReq.SetValueNumber(drpc.PoolSelfHealingAutoRebuild)
		default:
			return nil, errors.Errorf("unhandled self_heal type %q", healType)
		}
	default:
		return nil, errors.Errorf("unhandled pool property %q", propName)
	}

	return newReq, nil
}

// PoolSetProp forwards a request to the I/O server to set a pool property.
func (svc *mgmtSvc) PoolSetProp(ctx context.Context, req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropResp, error) {
	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req:%+v", req)

	newReq, err := resolvePoolPropVal(req)
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req (converted):%+v", newReq)

	var dresp *drpc.Response
	dresp, err = svc.makePoolServiceCall(ctx, drpc.MethodPoolSetProp, newReq)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolSetPropResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolSetProp response")
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, resp:%+v", resp)

	if resp.GetStatus() != 0 {
		return resp, nil
	}

	if resp.GetNumber() != newReq.GetNumber() {
		return nil, errors.Errorf("Response number doesn't match request (%d != %d)",
			resp.GetNumber(), newReq.GetNumber())
	}
	// Restore the string versions of the property/value
	resp.Property = &mgmtpb.PoolSetPropResp_Name{
		Name: req.GetName(),
	}
	if req.GetStrval() != "" {
		if resp.GetNumval() != newReq.GetNumval() {
			return nil, errors.Errorf("Response value doesn't match request (%d != %d)",
				resp.GetNumval(), newReq.GetNumval())
		}
		resp.Value = &mgmtpb.PoolSetPropResp_Strval{
			Strval: req.GetStrval(),
		}
	}

	return resp, nil
}

// PoolGetACL forwards a request to the IO server to fetch a pool's Access Control List
func (svc *mgmtSvc) PoolGetACL(ctx context.Context, req *mgmtpb.GetACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolGetACL dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolGetACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolGetACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolGetACL dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolOverwriteACL forwards a request to the IO server to overwrite a pool's Access Control List
func (svc *mgmtSvc) PoolOverwriteACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolOverwriteACL dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolOverwriteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolOverwriteACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolOverwriteACL dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolUpdateACL forwards a request to the IO server to add or update entries in
// a pool's Access Control List
func (svc *mgmtSvc) PoolUpdateACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolUpdateACL dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolUpdateACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolUpdateACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolUpdateACL dispatch, resp:%+v\n", resp)

	return resp, nil
}

// PoolDeleteACL forwards a request to the IO server to delete an entry from a
// pool's Access Control List.
func (svc *mgmtSvc) PoolDeleteACL(ctx context.Context, req *mgmtpb.DeleteACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolDeleteACL dispatch, req:%+v\n", req)

	dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolDeleteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDeleteACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolDeleteACL dispatch, resp:%+v\n", resp)

	return resp, nil
}

// ListPools forwards a gRPC request to the DAOS IO server to fetch a list of
// all pools in the system.
func (svc *mgmtSvc) ListPools(ctx context.Context, req *mgmtpb.ListPoolsReq) (*mgmtpb.ListPoolsResp, error) {
	svc.log.Debugf("MgmtSvc.ListPools dispatch, req:%+v\n", req)

	psList, err := svc.sysdb.PoolServiceList()
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.ListPoolsResp)
	for _, ps := range psList {
		resp.Pools = append(resp.Pools, &mgmtpb.ListPoolsResp_Pool{
			Uuid:    ps.PoolUUID.String(),
			Svcreps: system.RanksToUint32(ps.Replicas),
		})
	}

	svc.log.Debugf("MgmtSvc.ListPools dispatch, resp:%+v\n", resp)

	return resp, nil
}
