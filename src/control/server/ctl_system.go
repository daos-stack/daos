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
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	memberStopTimeout   = 10 * retryDelay
	prepShutdownTimeout = 10 * retryDelay
)

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

	// TODO DAOS-3647: update status of each system member if
	// !mi.IsStarted()

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

// prepShutdown requests registered harness to prepare their instances (system members)
// for system shutdown.
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) prepShutdown(ctx context.Context, leader *IOServerInstance) (system.MemberResults, error) {
	hostAddrs := svc.membership.Hosts()
	results := make(system.MemberResults, 0, len(hostAddrs)*maxIoServers)

	svc.log.Debugf("preparing ranks for shutdown on hosts: %v", hostAddrs)

	leaderMember, err := svc.membership.Get(leader.getSuperblock().Rank.Uint32())
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving system leader from membership")
	}

	for _, addr := range hostAddrs {
		if addr == leaderMember.Addr.String() {
			continue // leave leader's harness until last
		}

		hResults, err := harnessAction(ctx, leader.msClient,
			NewRemoteHarnessReq(HarnessPrepShutdown, addr))
		if err != nil {
			return nil, err
		}

		results = append(results, hResults...)
	}

	hResults, err := harnessAction(ctx, leader.msClient,
		NewRemoteHarnessReq(HarnessPrepShutdown, leaderMember.Addr.String()))
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
func (svc *ControlService) shutdown(ctx context.Context, leader *IOServerInstance) (system.MemberResults, error) {
	hostAddrs := svc.membership.Hosts()
	results := make(system.MemberResults, 0, len(hostAddrs)*maxIoServers)

	svc.log.Debugf("stopping ranks on hosts: %v", hostAddrs)

	leaderMember, err := svc.membership.Get(leader.getSuperblock().Rank.Uint32())
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving system leader from membership")
	}

	for _, addr := range hostAddrs {
		if addr == leaderMember.Addr.String() {
			continue // leave leader's harness until last
		}

		hResults, err := harnessAction(ctx, leader.msClient,
			NewRemoteHarnessReq(HarnessStop, addr))
		if err != nil {
			return nil, err
		}

		results = append(results, hResults...)
	}

	hResults, err := harnessAction(ctx, leader.msClient,
		NewRemoteHarnessReq(HarnessStop, leaderMember.Addr.String()))
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
	resp := &ctlpb.SystemStopResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debug("Received SystemStop RPC")

	if !mi.IsStarted() {
		return nil, errors.New("management service is not running")
	}

	// TODO: consider locking to prevent join attempts when shutting down

	if req.Prep {
		// prepare system members for shutdown
		prepResults, err := svc.prepShutdown(ctx, mi)
		if err != nil {
			return nil, err
		}
		if err := convert.Types(prepResults, &resp.Results); err != nil {
			return nil, err
		}
		if prepResults.HasErrors() {
			return resp, errors.New("PrepShutdown HasErrors")
		}
	}

	if req.Kill {
		// shutdown by stopping system members
		stopResults, err := svc.shutdown(ctx, mi)
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

// restart requests registered harnesses to restart their instances (system members)
// after a controlled shutdown using information in the membership registry.
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
//
// TODO: specify the ranks managed by the harness that should be started.
func (svc *ControlService) restart(ctx context.Context, leader *IOServerInstance) (system.MemberResults, error) {
	hostAddrs := svc.membership.Hosts()
	results := make(system.MemberResults, 0, len(hostAddrs)*maxIoServers)

	svc.log.Debugf("starting ranks on hosts: %v", hostAddrs)

	// retrieve rank from superblock to lookup stored member in membership
	leaderMember, err := svc.membership.Get(leader.getSuperblock().Rank.Uint32())
	if err != nil {
		return nil, errors.WithMessage(err, "retrieving system leader from membership")
	}

	// first start harness managing MS leader
	hResults, err := harnessAction(ctx, leader.msClient,
		NewRemoteHarnessReq(HarnessStart, leaderMember.Addr.String()))
	if err != nil {
		return nil, err
	}

	results = append(results, hResults...)

	for _, addr := range hostAddrs {
		if addr == leaderMember.Addr.String() {
			continue // leave leader's harness as it's already restarted
		}

		hResults, err := harnessAction(ctx, leader.msClient,
			NewRemoteHarnessReq(HarnessStart, addr))
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

	// target state will always be set in this scenario
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
	resp := &ctlpb.SystemStartResp{}

	// verify we are running on a host with the MS leader and therefore will
	// have membership list.
	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debug("Received SystemStart RPC")

	// start any stopped system members, note that instances will only
	// be started on hosts with all instances stopped
	startResults, err := svc.restart(ctx, mi)
	if err != nil {
		return nil, err
	}

	if err := convert.Types(startResults, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debug("Responding to SystemStart RPC")

	return resp, nil
}
