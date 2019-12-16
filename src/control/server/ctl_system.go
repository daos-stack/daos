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
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

const (
	memberStopTimeout   = 10 * retryDelay
	prepShutdownTimeout = 10 * retryDelay
)

// SystemMemberQuery implements the method defined for the Management Service.
//
// Return system membership list including member state.
func (svc *ControlService) SystemMemberQuery(ctx context.Context, req *ctlpb.SystemMemberQueryReq) (*ctlpb.SystemMemberQueryResp, error) {
	resp := &ctlpb.SystemMemberQueryResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	_, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("received SystemMemberQuery RPC; reporting DAOS system members (%+v)", svc.membership.Members())

	membersPB, err := common.MembersToPB(svc.membership.Members())
	if err != nil {
		return nil, err
	}
	resp.Members = membersPB

	svc.log.Debug("responding to SystemMemberQuery RPC")

	return resp, nil
}

// prepShutdown sends multicast PrepShutdown gRPC requests to system membership list.
func (svc *ControlService) prepShutdown(ctx context.Context, leader *IOServerInstance) common.SystemMemberResults {
	members := svc.membership.Members()
	results := make(common.SystemMemberResults, 0, len(members))

	// total retry timeout, allows for 10 retries
	ctx, _ = context.WithTimeout(ctx, prepShutdownTimeout)

	// TODO: parallelise and make async.
	for _, member := range members {
		svc.log.Debugf("MgmtSvc.prepShutdown member %+v\n", *member)

		result := common.NewSystemMemberResult(member.String(), "prep shutdown")

		resp, err := leader.msClient.PrepShutdown(ctx, member.Addr.String(),
			&mgmtpb.PrepShutdownReq{Rank: member.Rank})
		if err != nil {
			result.Err = err
		} else if resp.GetStatus() != 0 {
			result.Err = errors.Errorf("DAOS returned error code: %d\n",
				resp.GetStatus())
		}

		member.State = common.MemberStateStopping
		if result.Err != nil {
			member.State = common.MemberStateErrored
			svc.log.Errorf("MgmtSvc.prepShutdown error %s\n", result.Err)
		}
		svc.membership.Update(member)

		results = append(results, result)
	}

	return results
}

func (svc *ControlService) stopMember(ctx context.Context, leader *IOServerInstance, member *common.SystemMember) *common.SystemMemberResult {
	result := common.NewSystemMemberResult(member.String(), "stop")

	svc.log.Debugf("MgmtSvc.stopMembers kill member %+v\n", *member)

	// TODO: force should be applied if a number of retries fail
	resp, err := leader.msClient.Stop(ctx, member.Addr.String(),
		&mgmtpb.KillRankReq{Rank: member.Rank, Force: false})
	if err != nil {
		result.Err = err
	} else if resp.GetStatus() != 0 {
		result.Err = errors.Errorf("DAOS returned error code: %d\n",
			resp.GetStatus())
	}

	if result.Err == nil {
		svc.membership.Remove(member.Rank)
	} else {
		member.State = common.MemberStateErrored
		svc.membership.Update(member)
		svc.log.Errorf("MgmtSvc.stopMembers error %s\n", result.Err)
	}

	return result
}

// stopMembers sends multicast KillRank gRPC requests to system membership list.
func (svc *ControlService) stopMembers(ctx context.Context, leader *IOServerInstance) (common.SystemMemberResults, error) {
	members := svc.membership.Members()
	results := make(common.SystemMemberResults, 0, len(members))

	leaderMember, err := svc.membership.Get(leader.getSuperblock().Rank.Uint32())
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving system leader from membership")
	}

	// total retry timeout, allows for 10 retries
	ctx, _ = context.WithTimeout(ctx, memberStopTimeout)

	// TODO: parallelise and make async.
	for _, member := range members {
		if member.Rank == leaderMember.Rank {
			continue // leave leader until last
		}

		results = append(results, svc.stopMember(ctx, leader, member))
	}

	results = append(results, svc.stopMember(ctx, leader, leaderMember))

	return results, nil
}

// SystemStop implements the method defined for the Management Service.
//
// Initiate controlled shutdown of DAOS system.
func (svc *ControlService) SystemStop(ctx context.Context, req *ctlpb.SystemStopReq) (*ctlpb.SystemStopResp, error) {
	resp := &ctlpb.SystemStopResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debug("received SystemStop RPC; preparing to shutdown DAOS system")

	mi.Lock() // prevent join attempts when performing controlled shutdown
	defer mi.Unlock()

	// prepare system members for shutdown
	prepResults := svc.prepShutdown(ctx, mi)
	if prepResults.HasErrors() {
		resp.Results = common.MemberResultsToPB(prepResults)
		return resp, errors.New("PrepShutdown HasErrors")
	}

	svc.log.Debug("system ready for shutdown; stopping system members")

	// shutdown by stopping system members
	results, err := svc.stopMembers(ctx, mi)
	if err != nil {
		return nil, err
	}
	resp.Results = common.MemberResultsToPB(results)

	svc.log.Debug("responding to SystemStop RPC")

	return resp, nil
}
