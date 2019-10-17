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

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// SystemStop will perform a controlled shutdown of DAOS system and a list
// of remaining system members on failure.
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) SystemStop() (common.SystemMemberResults, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	rpcReq := &mgmtpb.SystemStopReq{}

	c.log.Debugf("DAOS system shutdown request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().SystemStop(context.Background(), rpcReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("DAOS system shutdown response: %s\n", rpcResp)

	return common.MemberResultsFromPB(c.log, rpcResp.Results), nil
}

// SystemMemberQuery will return the list of members joined to DAOS system.
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) SystemMemberQuery() (common.SystemMembers, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	rpcReq := &mgmtpb.SystemMemberQueryReq{}

	c.log.Debugf("DAOS system query request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().SystemMemberQuery(context.Background(), rpcReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("DAOS system query response: %s\n", rpcResp)

	return common.MembersFromPB(c.log, rpcResp.Members), nil
}

// KillRank Will terminate server running at given rank on pool specified by
// uuid. Request will only be issued to a single access point.
//
// Currently this is not exposed by control/cmd/dmg as a user command.
// TODO: consider usage model.
func (c *connList) KillRank(uuid string, rank uint32) ResultMap {
	var resp *mgmtpb.DaosResp
	var addr string
	results := make(ResultMap)

	mc, err := chooseServiceLeader(c.controllers)
	if err == nil {
		resp, err = mc.getSvcClient().KillRank(context.Background(),
			&mgmtpb.DaosRank{PoolUuid: uuid, Rank: rank})
		addr = mc.getAddress()
	}

	result := ClientResult{addr, resp, err}
	results[result.Address] = result

	return results
}
