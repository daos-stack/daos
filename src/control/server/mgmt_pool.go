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
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

const (
	// poolCreateRetryDelay defines the amount of time between pool create retries.
	// In the management service, the system map distribution code has a 3s backoff
	// for distribution errors.
	poolCreateRetryDelay = 1500 * time.Millisecond
)

// PoolCreate implements the method defined for the Management Service.
//
// Validate minimum SCM/NVMe pool size per VOS target, pool size request params
// are per-ioserver so need to be larger than (minimum_target_allocation *
// target_count).
func (svc *mgmtSvc) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq) (*mgmtpb.PoolCreateResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch, req:%+v\n", *req)

	mi, err := svc.harness.getMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	targetCount := mi.runner.GetConfig().TargetCount
	if targetCount == 0 {
		return nil, errors.New("zero target count")
	}
	if req.Scmbytes < ioserver.ScmMinBytesPerTarget*uint64(targetCount) {
		return nil, FaultPoolScmTooSmall(req.Scmbytes, targetCount)
	}
	if req.Nvmebytes != 0 && req.Nvmebytes < ioserver.NvmeMinBytesPerTarget*uint64(targetCount) {
		return nil, FaultPoolNvmeTooSmall(req.Nvmebytes, targetCount)
	}

	try := 0
	for {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		default:
		}

		dresp, err := svc.harness.CallDrpc(drpc.MethodPoolCreate, req)
		if err != nil {
			return nil, err
		}

		resp := &mgmtpb.PoolCreateResp{}
		if err = proto.Unmarshal(dresp.Body, resp); err != nil {
			return nil, errors.Wrap(err, "unmarshal PoolCreate response")
		}

		svc.log.Debugf("MgmtSvc.PoolCreate dispatch, try %d, resp:%+v\n", try, *resp)

		ds := drpc.DaosStatus(resp.GetStatus())
		switch ds {
		// retryable errors
		case drpc.DaosGroupVersionMismatch, drpc.DaosTimedOut:
			svc.log.Infof("MgmtSvc.PoolCreate (try %d), retrying due to %s", try, ds)
			try++
			select {
			case <-ctx.Done():
				return nil, ctx.Err()
			case <-time.After(poolCreateRetryDelay):
				continue
			}
		default:
			return resp, nil
		}
	}
}

// PoolDestroy implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDestroy(ctx context.Context, req *mgmtpb.PoolDestroyReq) (*mgmtpb.PoolDestroyResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.GetUuid() == "" {
		// TODO: do we want to validate pool exists via ListPools?
		return nil, errors.New("nil UUID")
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolDestroy, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolDestroyResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDestroy response")
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolEvict implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolEvict(ctx context.Context, req *mgmtpb.PoolEvictReq) (*mgmtpb.PoolEvictResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.GetUuid() == "" {
		return nil, errors.New("nil UUID")
	}

	svc.log.Debugf("MgmtSvc.PoolEvict dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolEvict, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolEvictResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolEvict response")
	}

	svc.log.Debugf("MgmtSvc.PoolEvict dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolExclude implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolExclude(ctx context.Context, req *mgmtpb.PoolExcludeReq) (*mgmtpb.PoolExcludeResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.GetUuid() == "" {
		// TODO: do we want to validate pool exists via ListPools?
		return nil, errors.New("nil UUID")
	}

	svc.log.Debugf("MgmtSvc.PoolExclude dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolExclude, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolExcludeResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolExclude response")
	}

	svc.log.Debugf("MgmtSvc.PoolExclude dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolDrain implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDrain(ctx context.Context, req *mgmtpb.PoolDrainReq) (*mgmtpb.PoolDrainResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.GetUuid() == "" {
		// TODO: do we want to validate pool exists via ListPools?
		return nil, errors.New("nil UUID")
	}

	svc.log.Debugf("MgmtSvc.PoolDrain dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolDrain, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolDrainResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDrain response")
	}

	svc.log.Debugf("MgmtSvc.PoolDrain dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolExtend implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolExtend(ctx context.Context, req *mgmtpb.PoolExtendReq) (*mgmtpb.PoolExtendResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.GetUuid() == "" {
		return nil, errors.New("nil UUID")
	}

	svc.log.Debugf("MgmtSvc.PoolExtend dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolExtend, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolExtendResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolExtend response")
	}

	svc.log.Debugf("MgmtSvc.PoolExtend dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolReintegrate implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolReintegrate(ctx context.Context, req *mgmtpb.PoolReintegrateReq) (*mgmtpb.PoolReintegrateResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.GetUuid() == "" {
		// TODO: do we want to validate pool exists via ListPools?
		return nil, errors.New("nil UUID")
	}

	svc.log.Debugf("MgmtSvc.PoolReintegrate dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolReintegrate, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolReintegrateResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolReintegrate response")
	}

	svc.log.Debugf("MgmtSvc.PoolReintegrate dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolQuery forwards a pool query request to the I/O server.
func (svc *mgmtSvc) PoolQuery(ctx context.Context, req *mgmtpb.PoolQueryReq) (*mgmtpb.PoolQueryResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	svc.log.Debugf("MgmtSvc.PoolQuery dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolQuery, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolQueryResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolQuery response")
	}

	svc.log.Debugf("MgmtSvc.PoolQuery dispatch, resp:%+v\n", *resp)

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

		return newReq, nil
	default:
		return nil, errors.Errorf("unhandled pool property %q", propName)
	}
}

// PoolSetProp forwards a request to the I/O server to set a pool property.
func (svc *mgmtSvc) PoolSetProp(ctx context.Context, req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropResp, error) {
	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req:%+v", *req)

	newReq, err := resolvePoolPropVal(req)
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req (converted):%+v", *newReq)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolSetProp, newReq)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolSetPropResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolSetProp response")
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, resp:%+v", *resp)

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
	svc.log.Debugf("MgmtSvc.PoolGetACL dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolGetACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolGetACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolGetACL dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolOverwriteACL forwards a request to the IO server to overwrite a pool's Access Control List
func (svc *mgmtSvc) PoolOverwriteACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolOverwriteACL dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolOverwriteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolOverwriteACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolOverwriteACL dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolUpdateACL forwards a request to the IO server to add or update entries in
// a pool's Access Control List
func (svc *mgmtSvc) PoolUpdateACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolUpdateACL dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolUpdateACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolUpdateACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolUpdateACL dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolDeleteACL forwards a request to the IO server to delete an entry from a
// pool's Access Control List.
func (svc *mgmtSvc) PoolDeleteACL(ctx context.Context, req *mgmtpb.DeleteACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolDeleteACL dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodPoolDeleteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDeleteACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolDeleteACL dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// ListPools forwards a gRPC request to the DAOS IO server to fetch a list of
// all pools in the system.
func (svc *mgmtSvc) ListPools(ctx context.Context, req *mgmtpb.ListPoolsReq) (*mgmtpb.ListPoolsResp, error) {
	svc.log.Debugf("MgmtSvc.ListPools dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(drpc.MethodListPools, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ListPoolsResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal ListPools response")
	}

	svc.log.Debugf("MgmtSvc.ListPools dispatch, resp:%+v\n", *resp)

	return resp, nil
}
