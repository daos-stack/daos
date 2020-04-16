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
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// PoolReintegrateReq struct contains request
type PoolReintegrateReq struct {
	UUID      string
	Rank      uint32
	Targetidx []uint32
}

// ReintegrateResp as no other parameters other than success/failure for now.

// PoolReintegrate will set a pool target for a specific rank back to up.
// This should automatically start the reintegration process.
// error (including any DER code from DAOS).
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) PoolReintegrate(req *PoolReintegrateReq) error {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return err
	}

	rpcReq := &mgmtpb.PoolReintegrateReq{Uuid: req.UUID, Rank: req.Rank, Targetidx: req.Targetidx}

	c.log.Debugf("Reintegrate DAOS pool target request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().PoolReintegrate(context.Background(), rpcReq)
	if err != nil {
		return err
	}

	c.log.Debugf("Reintegrate DAOS pool response: %s\n", rpcResp)

	if rpcResp.GetStatus() != 0 {
		return errors.Errorf("DAOS returned error code: %d\n",
			rpcResp.GetStatus())
	}

	return nil
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
