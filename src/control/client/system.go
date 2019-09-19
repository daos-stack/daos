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
	"golang.org/x/net/context"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/pkg/errors"
)

// SystemStopReq struct contains request
type SystemStopReq struct {
	ScmBytes   uint64
	NvmeBytes  uint64
	RankList   string
	NumSvcReps uint32
	Sys        string
	Usr        string
	Grp        string
	Acl        []string
}

// SystemStopResp struct contains response
type SystemStopResp struct {
	Uuid    string
	SvcReps string
}

// SystemStop will create a DAOS pool using provided parameters and return
// uuid, list of service replicas and error (including any DER code from DAOS).
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) SystemStop(req *SystemStopReq) (*SystemStopResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	rpcReq := &mgmtpb.SystemStopReq{
		Scmbytes: req.ScmBytes, Nvmebytes: req.NvmeBytes,
		Ranks: req.RankList, Numsvcreps: req.NumSvcReps, Sys: req.Sys,
		User: req.Usr, Usergroup: req.Grp, Acl: req.Acl,
	}

	c.log.Debugf("Create DAOS pool request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().SystemStop(context.Background(), rpcReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("Create DAOS pool response: %s\n", rpcResp)

	if rpcResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d\n",
			rpcResp.GetStatus())
	}

	return &SystemStopResp{Uuid: rpcResp.GetUuid(), SvcReps: rpcResp.GetSvcreps()}, nil
}
