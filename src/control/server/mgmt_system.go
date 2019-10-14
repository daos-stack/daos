//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"net"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

const stopTimeout = 10 * retryDelay

type addrErrors map[net.Addr]error

// systemStop sends multicast KillRank gRPC requests to system membership list.
func (svc *mgmtSvc) systemStop(ctx context.Context, leader *IOServerInstance) common.SystemMemberResults {
	members := svc.members.GetMembers()
	results := make(common.SystemMemberResults, 0, len(members))

	// total retry timeout, allows for 10 retries
	ctx, _ = context.WithTimeout(ctx, stopTimeout)

	// TODO: inhibit rebuild on pool services, parallelise and make async.
	for _, member := range members {
		svc.log.Debugf("MgmtSvc.systemStop kill member %+v\n", *member)

		result := common.SystemMemberResult{ID: member.String(), Action: "stop"}

		resp, err := leader.msClient.Stop(ctx, member.Addr.String(),
			&mgmtpb.DaosRank{Rank: member.Rank})
		if err != nil {
			result.Err = err
		} else if resp.GetStatus() != 0 {
			result.Err = errors.Errorf("DAOS returned error code: %d\n",
				resp.GetStatus())
		}

		if result.Err == nil {
			svc.members.Remove(member.Uuid)
		} else {
			svc.log.Debugf("MgmtSvc.systemStop error %s\n", result.Err)
		}

		results = append(results, &result)
	}

	return results
}

// KillRank implements the method defined for the Management Service.
//
// Stop data-plane instance managed by control-plane identified by unique rank.
func (svc *mgmtSvc) KillRank(ctx context.Context, req *mgmtpb.DaosRank) (*mgmtpb.DaosResp, error) {
	svc.log.Debugf("MgmtSvc.KillRank dispatch, req:%+v\n", *req)

	var mi *IOServerInstance
	for _, i := range svc.harness.Instances() {
		if i.hasSuperblock() && i.getSuperblock().Rank.Equals(ioserver.NewRankPtr(req.Rank)) {
			mi = i
			break
		}
	}

	if mi == nil {
		return nil, errors.Errorf("rank %d not found on this server", req.Rank)
	}

	dresp, err := mi.CallDrpc(mgmtModuleID, killRank, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.DaosResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal DAOS response")
	}

	svc.log.Debugf("MgmtSvc.KillRank dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// SystemMemberQuery implements the method defined for the Management Service.
//
// Return system membership list including member state.
func (svc *mgmtSvc) SystemMemberQuery(ctx context.Context, req *mgmtpb.SystemMemberQueryReq) (*mgmtpb.SystemMemberQueryResp, error) {
	resp := &mgmtpb.SystemMemberQueryResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	_, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debug("received SystemMemberQuery RPC; reporting DAOS system members")

	resp.Members = common.MembersToPB(svc.members.GetMembers())

	svc.log.Debug("responding to SystemMemberQuery RPC")

	return resp, nil
}

// SystemStop implements the method defined for the Management Service.
//
// Initiate controlled shutdown of DAOS system.
func (svc *mgmtSvc) SystemStop(ctx context.Context, req *mgmtpb.SystemStopReq) (*mgmtpb.SystemStopResp, error) {
	resp := &mgmtpb.SystemStopResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debug("received SystemStop RPC; proceeding to shutdown DAOS system")

	// perform controlled shutdown and populate response with results
	resp.Results = common.MemberResultsToPB(svc.systemStop(ctx, mi))

	svc.log.Debug("responding to SystemStop RPC")

	return resp, nil
}
