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
	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/system"
)

const systemReqTimeout = 30 * time.Second

// reportStoppedRanks populates relevant rank results indicating stopped state.
func (svc *ControlService) reportStoppedRanks(action string, ranks []system.Rank, err error) system.MemberResults {
	results := make(system.MemberResults, 0, len(ranks))
	for _, rank := range ranks {
		results = append(results, system.NewMemberResult(rank, action, err,
			system.MemberStateStopped))
	}

	return results
}

func (svc *ControlService) getMSMember() (*system.Member, error) {
	if svc.membership == nil || svc.harnessClient == nil {
		return nil, errors.New("host not an access point")
	}

	msInstance, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, errors.Wrap(err, "get MS instance")
	}
	if msInstance == nil {
		return nil, errors.New("MS instance not found")
	}

	rank, err := msInstance.GetRank()
	if err != nil {
		return nil, err
	}

	msMember, err := svc.membership.Get(rank)
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving MS member")
	}

	return msMember, nil
}

type temporary interface {
	Temporary() bool
}

func isUnreachableError(err error) bool {
	te, ok := errors.Cause(err).(temporary)
	if ok {
		return !te.Temporary()
	}

	return false
}

// updateMemberStatus requests registered harness to ping their instances (system
// members) in order to determine IO Server process responsiveness. Update membership
// appropriately.
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
func (svc *ControlService) updateMemberStatus(ctx context.Context, rankList []system.Rank) error {
	hostRanks := svc.membership.HostRanks(rankList...)

	svc.log.Debugf("updating response status for ranks %v", hostRanks)

	// Update either:
	// - members unresponsive to ping
	// - members with stopped processes
	// - members returning error from dRPC ping
	badRanks := make(map[system.Rank]system.MemberState)
	for addr, ranks := range hostRanks {
		hResults, err := svc.harnessClient.Query(ctx, addr, ranks...)
		if err != nil {
			if !isUnreachableError(err) {
				return err
			}

			for _, rank := range ranks {
				badRanks[rank] = system.MemberStateStopped
			}
			svc.log.Debugf("harness at %s is unreachable", addr)
			continue
		}

		for _, result := range hResults {
			if result.State == system.MemberStateUnresponsive ||
				result.State == system.MemberStateStopped ||
				result.State == system.MemberStateErrored {

				badRanks[result.Rank] = result.State
			}
		}
	}

	// only update members in the appropriate state (Started/Stopping)
	// leave unresponsive members to be updated by a join
	filteredMembers := svc.membership.Members(rankList, system.MemberStateEvicted,
		system.MemberStateErrored, system.MemberStateUnknown,
		system.MemberStateStopped, system.MemberStateUnresponsive)

	for _, m := range filteredMembers {
		if state, exists := badRanks[m.Rank]; exists {
			if err := svc.membership.SetMemberState(m.Rank, state); err != nil {
				return errors.Wrapf(err, "setting state of rank %d", m.Rank)
			}
		}
	}

	return nil
}

// SystemQuery implements the method defined for the Management Service.
//
// Return status of system members specified in request rank list (or all
// members if request rank list is empty).
func (svc *ControlService) SystemQuery(parent context.Context, req *ctlpb.SystemQueryReq) (*ctlpb.SystemQueryResp, error) {
	svc.log.Debug("Received SystemQuery RPC")

	_, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, errors.WithMessage(err, "query requires active MS")
	}

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	// Update and retrieve status of system members in rank list.
	rankList := system.RanksFromUint32(req.GetRanks())
	if err := svc.updateMemberStatus(ctx, rankList); err != nil {
		return nil, err
	}
	members := svc.membership.Members(rankList)

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
// one or more data-plane instances (DAOS system members) present in rankList.
func (svc *ControlService) prepShutdown(ctx context.Context, rankList []system.Rank) (system.MemberResults, error) {
	hostRanks := svc.membership.HostRanks(rankList...)
	results := make(system.MemberResults, 0, len(hostRanks)*maxIOServers)

	svc.log.Debugf("preparing ranks for shutdown on hosts: %v", hostRanks)

	msMember, err := svc.getMSMember()
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving MS member")
	}
	msAddr := msMember.Addr.String()
	msRank := msMember.Rank

	for addr, ranks := range hostRanks {
		if addr == msAddr {
			ranks = msRank.RemoveFromList(ranks)
		}
		if len(ranks) == 0 {
			continue
		}

		hResults, err := svc.harnessClient.PrepShutdown(ctx, addr, ranks...)
		if err != nil {
			if !isUnreachableError(err) {
				return nil, errors.Wrapf(err, "harness %s prep shutdown", addr)
			}

			hResults = svc.reportStoppedRanks("prep shutdown", ranks,
				errors.New("harness unresponsive"))
			svc.log.Debugf("no response from harness %s", addr)
		}
		results = append(results, hResults...)
	}

	// prep MS access point last if in rankList
	if msRank.InList(rankList) {
		hResults, err := svc.harnessClient.PrepShutdown(ctx, msAddr, msRank)
		if err != nil {
			return nil, err
		}
		results = append(results, hResults...)
	}

	if err := svc.membership.UpdateMemberStates(results); err != nil {

		return nil, err
	}

	return results, nil
}

// shutdown requests registered harnesses to stop its instances (system members).
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members) present in rankList.
func (svc *ControlService) shutdown(ctx context.Context, force bool, rankList []system.Rank) (system.MemberResults, error) {
	hostRanks := svc.membership.HostRanks(rankList...)
	results := make(system.MemberResults, 0, len(hostRanks)*maxIOServers)

	svc.log.Debugf("stopping ranks on hosts: %v", hostRanks)

	msMember, err := svc.getMSMember()
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving MS member")
	}
	msAddr := msMember.Addr.String()
	msRank := msMember.Rank

	for addr, ranks := range hostRanks {
		if addr == msAddr {
			ranks = msRank.RemoveFromList(ranks)
		}
		if len(ranks) == 0 {
			continue
		}

		hResults, err := svc.harnessClient.Stop(ctx, addr, force, ranks...)
		if err != nil {
			if !isUnreachableError(err) {
				return nil, errors.Wrapf(err, "harness %s stop", addr)
			}
			hResults = svc.reportStoppedRanks("stop", ranks, nil)
			svc.log.Debugf("no response from harness %s", addr)
		}
		results = append(results, hResults...)
	}

	// stop MS access point last if in rankList
	if msRank.InList(rankList) {
		hResults, err := svc.harnessClient.Stop(ctx, msAddr, force, msRank)
		if err != nil {
			return nil, err
		}
		results = append(results, hResults...)
	}

	if err := svc.membership.UpdateMemberStates(results); err != nil {
		return nil, err
	}

	return results, nil
}

// SystemStop implements the method defined for the Management Service.
//
// Initiate controlled shutdown of DAOS system.
func (svc *ControlService) SystemStop(parent context.Context, req *ctlpb.SystemStopReq) (*ctlpb.SystemStopResp, error) {
	svc.log.Debug("Received SystemStop RPC")

	resp := &ctlpb.SystemStopResp{}

	// TODO: consider locking to prevent join attempts when shutting down

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	if req.Prep {
		// prepare system members for shutdown
		prepResults, err := svc.prepShutdown(ctx,
			system.RanksFromUint32(req.GetRanks()))
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
		stopResults, err := svc.shutdown(ctx, req.Force,
			system.RanksFromUint32(req.GetRanks()))
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

// allRanksOnHostInList checks if all ranks managed by a host at "addr" are
// present in "rankList".
//
// Return the list of ranks managed by the host at "addr".
// If all are present return true, else false.
func (svc *ControlService) allRanksOnHostInList(addr string, rankList []system.Rank) ([]system.Rank, bool) {
	var rank system.Rank
	addrRanks := svc.membership.HostRanks()[addr]

	ok := true
	for _, rank = range addrRanks {
		if !rank.InList(rankList) {
			ok = false
			break
		}
	}
	if !ok {
		svc.log.Debugf("skip host %s: rank %d not in rank list %v",
			addr, rank, rankList)
	}

	return addrRanks, ok
}

// start requests registered harnesses to start their instances (system members)
// after a controlled shutdown using information in the membership registry.
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) start(ctx context.Context, rankList []system.Rank) (system.MemberResults, error) {
	hostRanks := svc.membership.HostRanks(rankList...)
	results := make(system.MemberResults, 0, len(hostRanks)*maxIOServers)

	svc.log.Debugf("starting ranks on hosts: %v", hostRanks)

	msMember, err := svc.getMSMember()
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving MS member")
	}
	msAddr := msMember.Addr.String()

	// first start harness managing MS member if all host ranks are in rankList
	//
	// TODO: when DAOS-4456 lands and ranks can be started independently
	//       of each other, check only the msRank is in rankList
	if ranks, ok := svc.allRanksOnHostInList(msAddr, rankList); ok {
		// Any ranks configured at addr will be started, specify all
		// ranks so we get relevant results back.
		//
		// TODO: when DAOS-4456 lands and ranks can be started independently
		//       of each other, start only the msRank here
		//msRank := msMember.Rank
		hResults, err := svc.harnessClient.Start(ctx, msAddr, ranks...)
		if err != nil {
			return nil, err
		}
		results = append(results, hResults...)
	}

	for addr, ranks := range hostRanks {
		// All ranks configured at addr will be started, therefore if
		// any of the harness ranks are not in rankList then don't start
		// harness.
		//
		// TODO: when DAOS-4456 lands and ranks can be started, remove
		//       below mitigation/code block
		newRanks, ok := svc.allRanksOnHostInList(addr, rankList)
		if !ok {
			continue
		}
		ranks = newRanks

		if addr == msAddr {
			// harnessClient.Start() will start all ranks on harness
			// so don't try to start msAddr again
			//
			// TODO: when DAOS-4456 lands and ranks can be started
			//       independently of each other on the same harness,
			//       remove the MS rank from the list so other rank
			//       on the MS harness will get started
			//ranks = msRank.RemoveFromList(ranks)
			continue
		}
		if len(ranks) == 0 {
			continue
		}

		hResults, err := svc.harnessClient.Start(ctx, addr, ranks...)
		if err != nil {
			if !isUnreachableError(err) {
				return nil, errors.Wrapf(err, "harness %s start", addr)
			}
			hResults = svc.reportStoppedRanks("start", ranks,
				errors.New("harness unresponsive"))
			svc.log.Debugf("no response from harness %s", addr)
		}
		results = append(results, hResults...)
	}

	// in the case of start, don't manually update member state to "started",
	// only to "ready". Member state will transition to "sarted" during
	// join or bootstrap
	filteredResults := make(system.MemberResults, 0, len(results))
	for _, r := range results {
		if r.Errored || r.State == system.MemberStateReady {
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
func (svc *ControlService) SystemStart(parent context.Context, req *ctlpb.SystemStartReq) (*ctlpb.SystemStartResp, error) {
	svc.log.Debug("Received SystemStart RPC")

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	// start any stopped system members, note that instances will only
	// be started on hosts with all instances stopped
	startResults, err := svc.start(ctx,
		system.RanksFromUint32(req.GetRanks()))
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
