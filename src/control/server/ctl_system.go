//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"sort"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	memberStopTimeout   = 10 * retryDelay
	prepShutdownTimeout = 10 * retryDelay
)

func (svc *ControlService) updateMembershipStatus(ctx context.Context, leader *IOServerInstance) error {
	for _, member := range svc.membership.Members() {
		// TODO: consider optimal Timeout value
		_, err := leader.msClient.Status(ctx, member.Addr.String(),
			&mgmtpb.PingRankReq{Rank: member.Rank})
		if err != nil {
			svc.log.Errorf("MgmtSvc.updateMemberStatus error %s\n", err)
			if err := svc.membership.SetMemberState(member.Rank,
				system.MemberStateUnresponsive); err != nil {

				return errors.Wrapf(err, "setting member state")
			}
		}
	}

	return nil
}

// SystemQuery implements the method defined for the Management Service.
//
// Return system status.
func (svc *ControlService) SystemQuery(ctx context.Context, req *ctlpb.SystemQueryReq) (*ctlpb.SystemQueryResp, error) {
	resp := &ctlpb.SystemQueryResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debug("Received SystemQuery RPC")

	// update status of each system member
	if err := svc.updateMembershipStatus(ctx, mi); err != nil {
		return nil, err
	}

	var members []*system.Member
	nilRank := ioserver.NilRank
	if nilRank.Equals(ioserver.NewRankPtr(req.Rank)) {
		members = svc.membership.Members()
	} else {
		member, err := svc.membership.Get(req.Rank)
		if err != nil {
			return nil, err
		}
		members = append(members, member)
	}

	if err := convert.Types(members, &resp.Members); err != nil {
		return nil, err
	}

	svc.log.Debug("Responding to SystemQuery RPC")

	return resp, nil
}

// prepShutdown sends multicast PrepShutdown gRPC requests to system membership list.
func (svc *ControlService) prepShutdown(ctx context.Context, leader *IOServerInstance) system.MemberResults {
	members := svc.membership.Members()
	results := make(system.MemberResults, 0, len(members))

	// total retry timeout, allows for 10 retries
	ctx, _ = context.WithTimeout(ctx, prepShutdownTimeout)

	// TODO: parallelise and make async.
	for _, member := range members {
		var resErr error

		resp, err := leader.msClient.PrepShutdown(ctx, member.Addr.String(),
			&mgmtpb.PrepShutdownReq{Rank: member.Rank})
		if err != nil {
			resErr = err
		} else if resp.GetStatus() != 0 {
			resErr = errors.Errorf("DAOS returned error code: %d\n",
				resp.GetStatus())
		}

		state := system.MemberStateStopping
		if resErr != nil {
			state = system.MemberStateErrored
			svc.log.Errorf("MgmtSvc.prepShutdown error %s\n", resErr)
		}
		if err := svc.membership.SetMemberState(member.Rank, state); err != nil {
			svc.log.Errorf("setting member state: %s", err)
		}

		results = append(results,
			system.NewMemberResult(member.Rank, "prep shutdown", resErr))
	}

	return results
}

func (svc *ControlService) stopMember(ctx context.Context, leader *IOServerInstance, member *system.Member) *system.MemberResult {
	var resErr error

	// TODO: force should be applied if a number of retries fail
	resp, err := leader.msClient.Stop(ctx, member.Addr.String(),
		&mgmtpb.KillRankReq{Rank: member.Rank, Force: false})
	if err != nil {
		resErr = err
	} else if resp.GetStatus() != 0 {
		resErr = errors.Errorf("DAOS returned error code: %d\n",
			resp.GetStatus())
	}

	state := system.MemberStateStopped
	if resErr != nil {
		state = system.MemberStateErrored
		svc.log.Errorf("MgmtSvc.stopMember error %s\n", resErr)
	}
	if err := svc.membership.SetMemberState(member.Rank, state); err != nil {
		svc.log.Errorf("setting member state: %s", err)
	}

	return system.NewMemberResult(member.Rank, "stop", resErr)
}

// stopMembers sends multicast KillRank gRPC requests to system membership list.
func (svc *ControlService) stopMembers(ctx context.Context, leader *IOServerInstance) (system.MemberResults, error) {
	members := svc.membership.Members()
	results := make(system.MemberResults, 0, len(members))

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

	svc.log.Debug("Received SystemStop RPC")

	// TODO: consider locking to prevent join attempts when shutting down

	if req.Prep {
		svc.log.Debug("Preparing to shutdown DAOS system")

		// prepare system members for shutdown
		prepResults := svc.prepShutdown(ctx, mi)
		if err := convert.Types(prepResults, &resp.Results); err != nil {
			return nil, err
		}
		if prepResults.HasErrors() {
			return resp, errors.New("PrepShutdown HasErrors")
		}
	}

	if req.Kill {
		svc.log.Debug("Stopping system members")

		// shutdown by stopping system members
		stopResults, err := svc.stopMembers(ctx, mi)
		if err != nil {
			return nil, err
		}
		if err := convert.Types(stopResults, &resp.Results); err != nil {
			return nil, err
		}
	}

	if resp.Results == nil {
		return nil, errors.New("response results not populated")
	}

	svc.log.Debug("Responding to SystemStop RPC")

	return resp, nil
}

// startRemoteHarness starts ranks managed by harness running on remote host.
//
// TODO: specify specific rank(s) to start in request
func (svc *ControlService) startRemoteHarness(ctx context.Context, leader *IOServerInstance, addr string, ranks ...uint32) error {
	req := mgmtpb.StartRanksReq{}
	req.Ranks = make([]uint32, 0, maxIoServers)

	if len(ranks) > maxIoServers {
		return errors.New("number of of ranks exceeds maximum")
	}
	if len(ranks) > 0 {
		for _, rank := range ranks {
			req.Ranks = append(req.Ranks, rank)
		}
	}

	// TODO: populate response with DER codes for each rank start.
	_, err := leader.msClient.Start(ctx, addr, &req)

	return err
}

func resolveLeaderMemberAddr(leader *IOServerInstance, members system.Members) (string, error) {
	var leaderMember *system.Member

	msAddr, err := leader.msClient.LeaderAddress()
	if err != nil {
		return "", err
	}

	aps, err := resolveAccessPoints([]string{msAddr})
	if err != nil {
		return "", err
	}

	for _, member := range members {
		if member.Addr.String() == aps[0].String() {
			leaderMember = member
		}
	}
	if leaderMember == nil {
		return "", errors.Errorf("MS leader address %s not found in membership",
			aps[0].String())
	}

	return leaderMember.Addr.String(), nil
}

// startMembers sends multicast StartRanks gRPC requests to host addresses in
// system membership list. Each host address represents a gRPC server associated
// with a harness managing one or more data-plane instances (DAOS system members).
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) startMembers(ctx context.Context, leader *IOServerInstance) (system.HarnessResults, error) {
	members := svc.membership.Members()
	results := make(map[string]error)

	leaderAddr, err := resolveLeaderMemberAddr(leader, members)
	if err != nil {
		return nil, errors.Wrap(err,
			"couldn't resolve harness address of MS leader")
	}

	// first start harness managing MS leader
	if err := svc.startRemoteHarness(ctx, leader, leaderAddr); err != nil {
		return nil, errors.Wrapf(err,
			"couldn't start harness managing MS leader at %s", leaderAddr)
	}
	results[leaderAddr] = nil

	// TODO: do we need to wait for the MS to be up before we start the rest?

	// start each harness once only
	for _, member := range members {
		addr := member.Addr.String()
		if _, exists := results[addr]; exists {
			continue
		}

		results[addr] = svc.startRemoteHarness(ctx, leader, addr)
	}

	addrs := make([]string, 0, len(results))
	for addr := range results {
		addrs = append(addrs, addr)
	}
	sort.Strings(addrs)

	startResults := make(system.HarnessResults, 0, len(results))
	for _, addr := range addrs {
		startResults = append(startResults,
			system.NewHarnessResult(addr, "start", results[addr]))
	}

	return startResults, nil
}

// SystemStart implements the method defined for the Management Service.
//
// Initiate controlled start of DAOS system.
//
// TODO: specify the specific ranks that should be started in request.
func (svc *ControlService) SystemStart(ctx context.Context, req *ctlpb.SystemStartReq) (*ctlpb.SystemStartResp, error) {
	resp := &ctlpb.SystemStartResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debug("Received SystemStart RPC; starting system members")

	// start stopped system members
	startResults, err := svc.startMembers(ctx, mi)
	if err != nil {
		return nil, err
	}

	// populate response
	if err := convert.Types(startResults, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debug("Responding to SystemStart RPC")

	return resp, nil
}
