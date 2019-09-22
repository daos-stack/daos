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
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// chooseServiceLeader will decide which connection to send request on.
//
// Currently expect only one connection to be available and return that.
func chooseServiceLeader(cs []Control) (Control, error) {
	if len(cs) == 0 {
		return nil, errors.New("no active connections")
	}

	// just return the first connection, expected to be the service leader
	return cs[0], nil
}

// PoolCreateReq struct contains request
type PoolCreateReq struct {
	ScmBytes   uint64
	NvmeBytes  uint64
	RankList   string
	NumSvcReps uint32
	Sys        string
	Usr        string
	Grp        string
	Acl        []string
}

// PoolCreateResp struct contains response
type PoolCreateResp struct {
	Uuid    string
	SvcReps string
}

// PoolCreate will create a DAOS pool using provided parameters and return
// uuid, list of service replicas and error (including any DER code from DAOS).
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) PoolCreate(req *PoolCreateReq) (*PoolCreateResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	rpcReq := &pb.PoolCreateReq{
		Scmbytes: req.ScmBytes, Nvmebytes: req.NvmeBytes,
		Ranks: req.RankList, Numsvcreps: req.NumSvcReps, Sys: req.Sys,
		User: req.Usr, Usergroup: req.Grp, Acl: req.Acl,
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

	return &PoolCreateResp{Uuid: rpcResp.GetUuid(), SvcReps: rpcResp.GetSvcreps()}, nil
}

// PoolDestroyReq struct contains request
type PoolDestroyReq struct {
	Uuid  string
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

	rpcReq := &pb.PoolDestroyReq{Uuid: req.Uuid, Force: req.Force}

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
