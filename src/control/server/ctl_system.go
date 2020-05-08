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
	"strings"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/system"
)

const systemReqTimeout = 30 * time.Second

// systemRanksFunc is an alias for control client API *Ranks() fanout
// function that executes across ranks on different hosts.
type systemRanksFunc func(context.Context, control.UnaryInvoker, *control.RanksReq) (*control.RanksResp, error)

type ranksMethod string

const (
	stop  ranksMethod = "stop"
	prep  ranksMethod = "prep shutdown"
	start ranksMethod = "start"
	ping  ranksMethod = "ping"
)

func getRanksFunc(method ranksMethod) systemRanksFunc {
	switch method {
	case stop:
		return control.StopRanks
	case prep:
		return control.PrepShutdownRanks
	case start:
		return control.StartRanks
	case ping:
		return control.PingRanks
	default:
		panic(1) // shouldn't happen
	}
}

// rpcToRanks sends requests to ranks in list on their respective host
// addresses through functions implementing UnaryInvoker.
func (svc *ControlService) rpcToRanks(ctx context.Context, req *control.RanksReq, method ranksMethod) (system.MemberResults, error) {
	if svc.membership == nil {
		return nil, errors.New("nil system membership")
	}
	if len(svc.membership.Ranks()) == 0 {
		return nil, errors.New("empty system membership")
	}
	if len(req.Ranks) == 0 {
		req.Ranks = svc.membership.Ranks() // empty rankList implies include all ranks
	}

	req.SetHostList(svc.membership.Hosts(req.Ranks...))
	svc.log.Debugf("host list %v", req.HostList)
	resp, err := getRanksFunc(method)(ctx, svc.rpcClient, req)
	if err != nil {
		return nil, err
	}

	results := make(system.MemberResults, 0, len(req.Ranks))
	results = append(results, resp.RankResults...)

	// synthesise "Stopped" rank results for any harness host errors
	hostRanks := svc.membership.HostRanks(req.Ranks...)
	for errMsg, hostSet := range resp.HostErrors {
		for _, addr := range strings.Split(hostSet.DerangedString(), ",") {
			// TODO: should annotate member state with "harness unresponsive" err
			for _, rank := range hostRanks[addr] {
				results = append(results,
					system.NewMemberResult(rank, string(method), errors.New(errMsg),
						system.MemberStateStopped))
			}
			svc.log.Debugf("harness %s (ranks %v) host error: %s",
				addr, hostRanks[addr], errMsg)
		}
	}

	return results, nil
}

// pingMembers requests registered harness to ping their instances (system
// members) in order to determine IO Server process responsiveness. Update membership
// appropriately.
//
// Each host address represents a gRPC server associated with a harness managing
// one or more data-plane instances (DAOS system members).
func (svc *ControlService) pingMembers(ctx context.Context, req *control.RanksReq) error {
	results, err := svc.rpcToRanks(ctx, req, ping)
	if err != nil {
		return err
	}

	// only update members in the appropriate state (Joined/Stopping)
	// leave unresponsive members to be updated by a join
	filteredMembers := svc.membership.Members(req.Ranks, system.MemberStateEvicted,
		system.MemberStateErrored, system.MemberStateUnknown,
		system.MemberStateStopped, system.MemberStateUnresponsive)

	for _, m := range filteredMembers {
		for _, r := range results {
			// Update either:
			// - members unresponsive to ping
			// - members with stopped processes
			// - members returning errors e.g. from dRPC ping
			if !r.Rank.Equals(m.Rank) ||
				(r.State != system.MemberStateUnresponsive &&
					r.State != system.MemberStateStopped &&
					r.State != system.MemberStateErrored) {

				continue
			}
			if err := svc.membership.SetMemberState(m.Rank, r.State); err != nil {
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
func (svc *ControlService) SystemQuery(parent context.Context, pbReq *ctlpb.SystemQueryReq) (*ctlpb.SystemQueryResp, error) {
	svc.log.Debug("Received SystemQuery RPC")

	if pbReq == nil {
		return nil, errors.New("nil request")
	}

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	req := &control.RanksReq{Ranks: system.RanksFromUint32(pbReq.GetRanks())}
	if err := svc.pingMembers(ctx, req); err != nil {
		return nil, err
	}
	members := svc.membership.Members(req.Ranks)

	pbResp := &ctlpb.SystemQueryResp{}
	if err := convert.Types(members, &pbResp.Members); err != nil {
		return nil, err
	}

	svc.log.Debug("Responding to SystemQuery RPC")

	return pbResp, nil
}

// SystemStop implements the method defined for the Management Service.
//
// Initiate controlled shutdown of DAOS system, return results for each rank.
func (svc *ControlService) SystemStop(parent context.Context, pbReq *ctlpb.SystemStopReq) (*ctlpb.SystemStopResp, error) {
	svc.log.Debug("Received SystemStop RPC")

	if pbReq == nil {
		return nil, errors.New("nil request")
	}

	req := &control.RanksReq{
		Ranks: system.RanksFromUint32(pbReq.GetRanks()),
		Force: pbReq.GetForce(),
	}
	pbResp := &ctlpb.SystemStopResp{}

	// TODO: consider locking to prevent join attempts when shutting down

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	if pbReq.GetPrep() {
		svc.log.Debug("preparing ranks for shutdown")

		results, err := svc.rpcToRanks(ctx, req, prep)
		if err != nil {
			return nil, err
		}
		if err = svc.membership.UpdateMemberStates(results); err != nil {
			return nil, err
		}
		if err = convert.Types(results, &pbResp.Results); err != nil {
			return nil, err
		}
		if !req.Force && results.HasErrors() {
			return pbResp, errors.New("PrepShutdown HasErrors")
		}
	}

	if pbReq.GetKill() {
		svc.log.Debug("shutting down ranks")

		results, err := svc.rpcToRanks(ctx, req, stop)
		if err != nil {
			return nil, err
		}
		if err = svc.membership.UpdateMemberStates(results); err != nil {
			return nil, err
		}
		if err = convert.Types(results, &pbResp.Results); err != nil {
			return nil, err
		}
	}

	if pbResp.GetResults() == nil {
		return nil, errors.New("response results not populated")
	}

	svc.log.Debug("Responding to SystemStop RPC")

	return pbResp, nil
}

// SystemStart implements the method defined for the Management Service.
//
// Initiate controlled start of DAOS system instances (system members)
// after a controlled shutdown using information in the membership registry.
// Return system start results.
func (svc *ControlService) SystemStart(parent context.Context, pbReq *ctlpb.SystemStartReq) (*ctlpb.SystemStartResp, error) {
	svc.log.Debug("Received SystemStart RPC")

	if pbReq == nil {
		return nil, errors.New("nil request")
	}

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	req := &control.RanksReq{Ranks: system.RanksFromUint32(pbReq.GetRanks())}
	results, err := svc.rpcToRanks(ctx, req, start)
	if err != nil {
		return nil, err
	}
	if err := svc.membership.UpdateMemberStates(results); err != nil {
		return nil, err
	}

	pbResp := &ctlpb.SystemStartResp{}
	if err := convert.Types(results, &pbResp.Results); err != nil {
		return nil, err
	}

	svc.log.Debug("Responding to SystemStart RPC")

	return pbResp, nil
}
