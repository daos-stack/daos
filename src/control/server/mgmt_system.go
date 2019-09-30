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
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// systemQuery retrieves system membership list.
func (svc *mgmtSvc) systemQuery() []*common.SystemMember {
	// return list of hosts registered through gRPC join requests
	return svc.members
}

// systemStop sends multicast KillRank gRPC requests to system membership list.
func (svc *mgmtSvc) systemStop(ctx context.Context, leader *IOServerInstance) error {
	// TODO: inhibit rebuild on pool services, parallelise and make async.
	for _, member := range svc.members {
		svc.log.Debugf("MgmtSvc.systemStop murder member %+v\n", *member)
		resp, err := leader.msClient.Stop(ctx, member.Addr, &mgmtpb.DaosRank{
			Rank: member.Rank,
		})
		if err != nil {
			svc.log.Debugf("MgmtSvc.systemStop error %s\n", err)
			// TODO: record errors and continue
			return err
		}

		svc.log.Debugf("MgmtSvc.systemStop response %+v\n", *resp)
	}

	return nil
}

// KillRank implements the method defined for the Management Service.
//
// Stop data-plane instance managed by control-plane identified by unique rank.
func (svc *mgmtSvc) KillRank(ctx context.Context, req *mgmtpb.DaosRank) (*mgmtpb.DaosResp, error) {
	mi, err := svc.harness.GetManagementInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.KillRank dispatch, req:%+v\n", *req)

	dresp, err := makeDrpcCall(mi.drpcClient, mgmtModuleID, killRank, req)
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

// SystemQuery implements the method defined for the Management Service.
//
// Return system membership list including member state.
func (svc *mgmtSvc) SystemQuery(ctx context.Context, req *mgmtpb.SystemQueryReq) (*mgmtpb.SystemQueryResp, error) {
	resp := &mgmtpb.SystemQueryResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	_, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debug("received SystemQuery RPC; reporting DAOS system members")

	// no lock as we are just taking a read-only snapshot
	resp.Members = common.SystemMembersToPB(svc.systemQuery())

	svc.log.Debug("responding to SystemQuery RPC")

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

	// perform controlled shutdown (synchronous)
	if err := svc.systemStop(ctx, mi); err != nil {
		return nil, err
	}
	// no lock as we are just taking a read-only snapshot
	resp.Members = common.SystemMembersToPB(svc.systemQuery())

	svc.log.Debug("responding to SystemStop RPC")

	return resp, nil
}
