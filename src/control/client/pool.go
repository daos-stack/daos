//
// (C) Copyright 2019-2020 Intel Corporation.
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

package client

import (
	"strconv"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// PoolCreateReq struct contains request
type PoolCreateReq struct {
	ScmBytes   uint64
	NvmeBytes  uint64
	RankList   []uint32
	NumSvcReps uint32
	Sys        string
	Usr        string
	Grp        string
	ACL        *common.AccessControlList
	UUID       string
}

// PoolCreateResp struct contains response
type PoolCreateResp struct {
	UUID    string
	SvcReps []uint32
}

// PoolCreate will create a DAOS pool using provided parameters and generated
// UUID. Return values will be UUID, list of service replicas and error
// (including any DER code from DAOS).
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) PoolCreate(req *PoolCreateReq) (*PoolCreateResp, error) {
	// NB: This method is a stub to indicate that it has been replaced
	// by functionality in the control API, and will be removed when
	// the client package is removed.
	return nil, nil
}

// PoolDestroyReq struct contains request
type PoolDestroyReq struct {
	UUID  string
	Force bool
}

// No PoolDestroyResp as no other parameters other than success/failure.

// PoolDestroy will Destroy a DAOS pool identified by its uuid and returns
// error (including any DER code from DAOS).
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) PoolDestroy(req *PoolDestroyReq) error {
	// NB: This method is a stub to indicate that it has been replaced
	// by functionality in the control API, and will be removed when
	// the client package is removed.
	return nil
}

type (
	// PoolQueryReq contains pool query parameters.
	PoolQueryReq struct {
		UUID string
	}

	// StorageUsageStats represents DAOS storage usage statistics.
	StorageUsageStats struct {
		Total uint64
		Free  uint64
		Min   uint64
		Max   uint64
		Mean  uint64
	}

	// PoolRebuildState indicates the current state of the pool rebuild process.
	PoolRebuildState uint

	// PoolRebuildStatus contains detailed information about the pool rebuild process.
	PoolRebuildStatus struct {
		Status  int32
		State   PoolRebuildState
		Objects uint64
		Records uint64
	}

	// PoolQueryResp contains the pool query response.
	PoolQueryResp struct {
		Status          int32
		UUID            string
		TotalTargets    uint32
		ActiveTargets   uint32
		DisabledTargets uint32
		Rebuild         *PoolRebuildStatus
		Scm             *StorageUsageStats
		Nvme            *StorageUsageStats
	}
)

const (
	// PoolRebuildStateIdle indicates that the rebuild process is idle.
	PoolRebuildStateIdle PoolRebuildState = iota
	// PoolRebuildStateDone indicates that the rebuild process has completed.
	PoolRebuildStateDone
	// PoolRebuildStateBusy indicates that the rebuild process is in progress.
	PoolRebuildStateBusy
)

func (prs PoolRebuildState) String() string {
	return [...]string{"idle", "done", "busy"}[prs]
}

// PoolQuery performs a query against the pool service.
func (c *connList) PoolQuery(req PoolQueryReq) (*PoolQueryResp, error) {
	// NB: This method is a stub to indicate that it has been replaced
	// by functionality in the control API, and will be removed when
	// the client package is removed.
	return nil, nil
}

// PoolSetPropReq contains pool set-prop parameters.
type PoolSetPropReq struct {
	// UUID identifies the pool for which this property should be set.
	UUID string
	// Property is always a string representation of the pool property.
	// It will be resolved into the C representation prior to being
	// forwarded over dRPC.
	Property string
	// Value is an approximation of the union in daos_prop_entry.
	// It can be either a string or a uint64. Struct-based properties
	// are not supported via this API.
	Value interface{}
}

// SetString sets the property value to a string.
func (pspr *PoolSetPropReq) SetString(strVal string) {
	pspr.Value = strVal
}

// SetNumber sets the property value to a uint64 number.
func (pspr *PoolSetPropReq) SetNumber(numVal uint64) {
	pspr.Value = numVal
}

// PoolSetPropResp contains the response to a pool set-prop operation.
type PoolSetPropResp struct {
	UUID     string
	Property string
	Value    string
}

// PoolSetProp sends a pool set-prop request to the pool service leader.
func (c *connList) PoolSetProp(req PoolSetPropReq) (*PoolSetPropResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	if req.Property == "" {
		return nil, errors.Errorf("invalid property name %q", req.Property)
	}

	rpcReq := &mgmtpb.PoolSetPropReq{
		Uuid: req.UUID,
	}
	rpcReq.SetPropertyName(req.Property)

	switch val := req.Value.(type) {
	case string:
		rpcReq.SetValueString(val)
	case uint64:
		rpcReq.SetValueNumber(val)
	default:
		return nil, errors.Errorf("unhandled property value: %+v", req.Value)
	}

	c.log.Debugf("DAOS pool setprop request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().PoolSetProp(context.Background(), rpcReq)
	if err != nil {
		return nil, errors.Wrap(err, "PoolSetProp failed")
	}

	c.log.Debugf("DAOS pool setprop response: %s\n", rpcResp)

	if rpcResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d\n",
			rpcResp.GetStatus())
	}

	resp := &PoolSetPropResp{
		UUID:     req.UUID,
		Property: rpcResp.GetName(),
	}

	switch v := rpcResp.GetValue().(type) {
	case *mgmtpb.PoolSetPropResp_Strval:
		resp.Value = v.Strval
	case *mgmtpb.PoolSetPropResp_Numval:
		resp.Value = strconv.FormatUint(v.Numval, 10)
	default:
		return nil, errors.Errorf("unable to represent response value %+v", rpcResp.Value)
	}

	return resp, nil
}

// PoolGetACLReq contains the input parameters for PoolGetACL
type PoolGetACLReq struct {
	UUID string // pool UUID
}

// PoolGetACLResp contains the output results for PoolGetACL
type PoolGetACLResp struct {
	ACL *common.AccessControlList
}

// PoolGetACL gets the Access Control List for the pool.
func (c *connList) PoolGetACL(req PoolGetACLReq) (*PoolGetACLResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	pbReq := &mgmtpb.GetACLReq{Uuid: req.UUID}

	c.log.Debugf("Get DAOS pool ACL request: %v", pbReq)

	pbResp, err := mc.getSvcClient().PoolGetACL(context.Background(), pbReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("Get DAOS pool ACL response: %v", pbResp)

	if pbResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d",
			pbResp.GetStatus())
	}

	return &PoolGetACLResp{
		ACL: proto.AccessControlListFromPB(pbResp),
	}, nil
}

// PoolOverwriteACLReq contains the input parameters for PoolOverwriteACL
type PoolOverwriteACLReq struct {
	UUID string                    // pool UUID
	ACL  *common.AccessControlList // new ACL for the pool
}

// PoolOverwriteACLResp returns the updated ACL for the pool
type PoolOverwriteACLResp struct {
	ACL *common.AccessControlList // actual ACL of the pool
}

// PoolOverwriteACL sends a request to replace the pool's old Access Control List
// with a new one. If it succeeds, it returns the updated ACL. If not, it returns
// an error.
func (c *connList) PoolOverwriteACL(req PoolOverwriteACLReq) (*PoolOverwriteACLResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	pbReq := &mgmtpb.ModifyACLReq{Uuid: req.UUID}
	if !req.ACL.Empty() {
		pbReq.ACL = req.ACL.Entries
	}

	c.log.Debugf("Overwrite DAOS pool ACL request: %v", pbReq)

	pbResp, err := mc.getSvcClient().PoolOverwriteACL(context.Background(), pbReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("Overwrite DAOS pool ACL response: %v", pbResp)

	if pbResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d",
			pbResp.GetStatus())
	}

	return &PoolOverwriteACLResp{
		ACL: proto.AccessControlListFromPB(pbResp),
	}, nil
}

// PoolUpdateACLReq contains the input parameters for PoolUpdateACL
type PoolUpdateACLReq struct {
	UUID string                    // pool UUID
	ACL  *common.AccessControlList // ACL entries to add to the pool
}

// PoolUpdateACLResp returns the updated ACL for the pool
type PoolUpdateACLResp struct {
	ACL *common.AccessControlList // actual ACL of the pool
}

// PoolUpdateACL sends a request to add new entries and update existing entries
// in a pool's Access Control List. If it succeeds, it returns the updated ACL.
// If not, it returns an error.
func (c *connList) PoolUpdateACL(req PoolUpdateACLReq) (*PoolUpdateACLResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	if req.ACL.Empty() {
		return nil, errors.New("no entries requested")
	}

	pbReq := &mgmtpb.ModifyACLReq{Uuid: req.UUID}
	pbReq.ACL = req.ACL.Entries

	c.log.Debugf("Update DAOS pool ACL request: %v", pbReq)

	pbResp, err := mc.getSvcClient().PoolUpdateACL(context.Background(), pbReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("Update DAOS pool ACL response: %v", pbResp)

	if pbResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d",
			pbResp.GetStatus())
	}

	return &PoolUpdateACLResp{
		ACL: proto.AccessControlListFromPB(pbResp),
	}, nil
}

// PoolDeleteACLReq contains the input parameters for PoolDeleteACL.
type PoolDeleteACLReq struct {
	UUID      string // UUID of the pool
	Principal string // Principal whose entry will be removed
}

// PoolDeleteACLResp returns the updated ACL for the pool.
type PoolDeleteACLResp struct {
	ACL *common.AccessControlList // actual ACL of the pool
}

// PoolDeleteACL sends a request to delete an entry in a pool's Access Control
// List. If it succeeds, it returns the updated ACL. If it fails, it returns an
// error.
func (c *connList) PoolDeleteACL(req PoolDeleteACLReq) (*PoolDeleteACLResp, error) {
	if req.Principal == "" {
		return nil, errors.New("no principal provided")
	}

	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	pbReq := &mgmtpb.DeleteACLReq{Uuid: req.UUID, Principal: req.Principal}

	c.log.Debugf("Delete DAOS pool ACL request: %v", pbReq)

	pbResp, err := mc.getSvcClient().PoolDeleteACL(context.Background(), pbReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("Delete DAOS pool ACL response: %v", pbResp)

	if pbResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d",
			pbResp.GetStatus())
	}

	return &PoolDeleteACLResp{
		ACL: proto.AccessControlListFromPB(pbResp),
	}, nil
}
