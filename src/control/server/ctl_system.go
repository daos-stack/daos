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
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/system"
)

func (svc *ControlService) getMSMemberAddress() (string, error) {
	if svc.membership == nil || svc.harnessClient == nil {
		return "", errors.New("host not an access point")
	}

	msInstance, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return "", errors.Wrap(err, "get MS instance")
	}
	if msInstance == nil {
		return "", errors.New("MS instance not found")
	}

	if !msInstance.hasSuperblock() {
		return "", errors.New("MS instance has no superblock")
	}

	msMember, err := svc.membership.Get(msInstance.getSuperblock().Rank.Uint32())
	if err != nil {
		return "", errors.WithMessage(err, "retrieving MS member")
	}

	return msMember.Addr.String(), nil
}

// updateMemberStatus requests registered harness to ping their instances (system
// members) in order to determine IO Server process responsiveness. Update membership
// appropriately.
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) updateMemberStatus(ctx context.Context) error {
	// exclude members with states that can't be updated with response check
	statesToExclude := []system.MemberState{
		system.MemberStateEvicted, system.MemberStateErrored,
		system.MemberStateUnknown, system.MemberStateStopped,
		system.MemberStateUnresponsive,
	}
	hostAddrs := svc.membership.Hosts(statesToExclude...)

	svc.log.Debugf("updating response status for ranks on hosts: %v", hostAddrs)

	// Update either:
	// - members unresponsive to ping
	// - members with stopped processes
	// TODO: update members with ping errors
	badRanks := make(map[uint32]system.MemberState)
	for _, addr := range hostAddrs {
		hResults, err := svc.harnessClient.Query(ctx, addr)
		if err != nil {
			return err
		}

		for _, result := range hResults {
			if result.State == system.MemberStateUnresponsive ||
				result.State == system.MemberStateStopped {

				badRanks[result.Rank] = result.State
			}
		}
	}

	// only update members in the appropriate state (Started/Stopping)
	// leave unresponsive members to be updated by a join
	filteredMembers := svc.membership.Members(statesToExclude...)

	for _, m := range filteredMembers {
		if state, exists := badRanks[m.Rank]; exists {
			if err := svc.membership.SetMemberState(m.Rank, state); err != nil {
				return err
			}
		}
	}

	return nil
}

// SystemQuery implements the method defined for the Management Service.
//
// Return system status.
func (svc *ControlService) SystemQuery(ctx context.Context, req *ctlpb.SystemQueryReq) (*ctlpb.SystemQueryResp, error) {
	svc.log.Debug("Received SystemQuery RPC")

	_, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	// Update status of each system member.
	// TODO: Should only given rank be updated if supplied in request?
	if err := svc.updateMemberStatus(ctx); err != nil {
		return nil, err
	}

	var members []*system.Member
	// negative Rank in request indicates none specified
	if req.Rank < 0 {
		members = svc.membership.Members()
	} else {
		member, err := svc.membership.Get(uint32(req.Rank))
		if err != nil {
			return nil, err
		}
		members = append(members, member)
	}

	resp := &ctlpb.SystemQueryResp{}
	if err := convert.Types(members, &resp.Members); err != nil {
		return nil, err
	}

	svc.log.Debug("Responding to SystemQuery RPC")

	return resp, nil
}

// prepShutdown requests registered harness to prepare their instances (system members)
// for system shutdown.
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) prepShutdown(ctx context.Context) (system.MemberResults, error) {
	hostAddrs := svc.membership.Hosts()
	results := make(system.MemberResults, 0, len(hostAddrs)*maxIoServers)

	svc.log.Debugf("preparing ranks for shutdown on hosts: %v", hostAddrs)

	msAddr, err := svc.getMSMemberAddress()
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving MS address")
	}

	for _, addr := range hostAddrs {
		if addr == msAddr {
			continue // leave MS harness until last
		}

		hResults, err := svc.harnessClient.PrepShutdown(ctx, addr)
		if err != nil {
			return nil, err
		}

		results = append(results, hResults...)
	}

	hResults, err := svc.harnessClient.PrepShutdown(ctx, msAddr)
	if err != nil {
		return nil, err
	}

	results = append(results, hResults...)

	if err := svc.membership.UpdateMemberStates(results); err != nil {

		return nil, err
	}

	return results, nil
}

// shutdown requests registered harnesses to stop its instances (system members).
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) shutdown(ctx context.Context, force bool) (system.MemberResults, error) {
	hostAddrs := svc.membership.Hosts()
	results := make(system.MemberResults, 0, len(hostAddrs)*maxIoServers)

	svc.log.Debugf("stopping ranks on hosts: %v", hostAddrs)

	msAddr, err := svc.getMSMemberAddress()
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving MS address")
	}

	for _, addr := range hostAddrs {
		if addr == msAddr {
			continue // leave MS harness until last
		}

		hResults, err := svc.harnessClient.Stop(ctx, addr, force)
		if err != nil {
			return nil, err
		}

		results = append(results, hResults...)
	}

	hResults, err := svc.harnessClient.Stop(ctx, msAddr, force)
	if err != nil {
		return nil, err
	}

	results = append(results, hResults...)

	if err := svc.membership.UpdateMemberStates(results); err != nil {
		return nil, err
	}

	return results, nil
}

// SystemStop implements the method defined for the Management Service.
//
// Initiate controlled shutdown of DAOS system.
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) SystemStop(ctx context.Context, req *ctlpb.SystemStopReq) (*ctlpb.SystemStopResp, error) {
	svc.log.Debug("Received SystemStop RPC")

	resp := &ctlpb.SystemStopResp{}

	// TODO: consider locking to prevent join attempts when shutting down

	if req.Prep {
		// prepare system members for shutdown
		prepResults, err := svc.prepShutdown(ctx)
		if err != nil {
			return nil, err
		}
		if err := convert.Types(prepResults, &resp.Results); err != nil {
			return nil, err
		}
		if !req.Force && prepResults.HasErrors() {
			return resp, errors.New("PrepShutdown HasErrors")
		}
	}

	if req.Kill {
		// shutdown by stopping system members
		stopResults, err := svc.shutdown(ctx, req.Force)
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

// start requests registered harnesses to start their instances (system members)
// after a controlled shutdown using information in the membership registry.
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) start(ctx context.Context) (system.MemberResults, error) {
	hostAddrs := svc.membership.Hosts()
	results := make(system.MemberResults, 0, len(hostAddrs)*maxIoServers)

	svc.log.Debugf("starting ranks on hosts: %v", hostAddrs)

	msAddr, err := svc.getMSMemberAddress()
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving MS address")
	}

	// first start harness managing MS member
	hResults, err := svc.harnessClient.Start(ctx, msAddr)
	if err != nil {
		return nil, err
	}

	results = append(results, hResults...)

	for _, addr := range hostAddrs {
		if addr == msAddr {
			continue // MS member harness already started
		}

		hResults, err := svc.harnessClient.Start(ctx, addr)
		if err != nil {
			return nil, err
		}

		results = append(results, hResults...)
	}

	// in the case of start, don't manually update member states, members
	// are updated as they join or bootstrap, only update state on errors
	filteredResults := make(system.MemberResults, 0, len(results))
	for _, r := range results {
		if r.Errored {
			filteredResults = append(filteredResults, r)
		}
	}

	if err := svc.membership.UpdateMemberStates(filteredResults); err != nil {
		return nil, err
	}

	return results, nil
}

// SystemStart implements the method defined for the Management Service.
//
// Initiate controlled start of DAOS system.
//
// TODO: specify the specific ranks that should be started in request.
func (svc *ControlService) SystemStart(ctx context.Context, req *ctlpb.SystemStartReq) (*ctlpb.SystemStartResp, error) {
	svc.log.Debug("Received SystemStart RPC")

	// start any stopped system members, note that instances will only
	// be started on hosts with all instances stopped
	startResults, err := svc.start(ctx)
	if err != nil {
		return nil, err
	}

	resp := &ctlpb.SystemStartResp{}
	if err := convert.Types(startResults, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debug("Responding to SystemStart RPC")

	return resp, nil
}
