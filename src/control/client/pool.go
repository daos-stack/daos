//
// (C) Copyright 2019 Intel Corporation.
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
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
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
	ACL        *AccessControlList
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
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	poolUUID, err := uuid.NewRandom()
	if err != nil {
		return nil, errors.Wrap(err, "generating pool uuid")
	}
	poolUUIDStr := poolUUID.String()

	rpcReq := &mgmtpb.PoolCreateReq{
		Scmbytes: req.ScmBytes, Nvmebytes: req.NvmeBytes, Ranks: req.RankList,
		Numsvcreps: req.NumSvcReps, Sys: req.Sys, User: req.Usr,
		Usergroup: req.Grp, Uuid: poolUUIDStr,
	}

	if !req.ACL.Empty() {
		rpcReq.Acl = req.ACL.Entries
	}

	c.log.Debugf("Create DAOS pool request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().PoolCreate(context.Background(), rpcReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("Create DAOS pool response: %s\n", rpcResp)

	if rpcResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d\n",
			rpcResp.GetStatus())
	}

	return &PoolCreateResp{UUID: poolUUIDStr, SvcReps: rpcResp.GetSvcreps()}, nil
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
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return err
	}

	rpcReq := &mgmtpb.PoolDestroyReq{Uuid: req.UUID, Force: req.Force}

	c.log.Debugf("Destroy DAOS pool request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().PoolDestroy(context.Background(), rpcReq)
	if err != nil {
		return err
	}

	c.log.Debugf("Destroy DAOS pool response: %s\n", rpcResp)

	if rpcResp.GetStatus() != 0 {
		return errors.Errorf("DAOS returned error code: %d\n",
			rpcResp.GetStatus())
	}

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
		Status        int32
		UUID          string
		TotalTargets  uint32
		ActiveTargets uint32
		Disabled      bool
		Rebuild       *PoolRebuildStatus
		Scm           *StorageUsageStats
		Nvme          *StorageUsageStats
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
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	rpcReq := &mgmtpb.PoolQueryReq{
		Uuid: req.UUID,
	}

	c.log.Debugf("DAOS pool query request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().PoolQuery(context.Background(), rpcReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("DAOS pool query response: %s\n", rpcResp)

	if rpcResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d\n",
			rpcResp.GetStatus())
	}

	resp := new(PoolQueryResp)
	if err := convert.Types(rpcResp, resp); err != nil {
		return nil, errors.Wrap(err, "failed to convert from proto to native")
	}

	return resp, nil
}

// PoolGetACLReq contains the input parameters for PoolGetACL
type PoolGetACLReq struct {
	UUID string // pool UUID
}

// PoolGetACLResp contains the output results for PoolGetACL
type PoolGetACLResp struct {
	ACL *AccessControlList
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
		ACL: &AccessControlList{Entries: pbResp.ACL},
	}, nil
}

// PoolOverwriteACLReq contains the input parameters for PoolOverwriteACL
type PoolOverwriteACLReq struct {
	UUID string             // pool UUID
	ACL  *AccessControlList // new ACL for the pool
}

// PoolOverwriteACLResp returns the updated ACL for the pool
type PoolOverwriteACLResp struct {
	ACL *AccessControlList // actual ACL of the pool
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
		ACL: &AccessControlList{Entries: pbResp.ACL},
	}, nil
}

// PoolUpdateACLReq contains the input parameters for PoolUpdateACL
type PoolUpdateACLReq struct {
	UUID string             // pool UUID
	ACL  *AccessControlList // ACL entries to add to the pool
}

// PoolUpdateACLResp returns the updated ACL for the pool
type PoolUpdateACLResp struct {
	ACL *AccessControlList // actual ACL of the pool
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
		ACL: &AccessControlList{Entries: pbResp.ACL},
	}, nil
}

// PoolDeleteACLReq contains the input parameters for PoolDeleteACL.
type PoolDeleteACLReq struct {
	UUID      string // UUID of the pool
	Principal string // Principal whose entry will be removed
}

// PoolDeleteACLResp returns the updated ACL for the pool.
type PoolDeleteACLResp struct {
	ACL *AccessControlList // actual ACL of the pool
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
		ACL: &AccessControlList{Entries: pbResp.ACL},
	}, nil
}
