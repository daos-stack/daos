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
	"net"
	"strings"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/system"
)

const systemReqTimeout = 30 * time.Second

type (
	// systemRanksFunc is an alias for control client API *Ranks() fanout
	// function that executes across ranks on different hosts.
	systemRanksFunc func(context.Context, control.UnaryInvoker, *control.RanksReq) (*control.RanksResp, error)

	fanoutRequest struct {
		Method       systemRanksFunc
		Hosts, Ranks string
		Force        bool
	}

	fanoutResponse struct {
		Results     system.MemberResults
		AbsentHosts *hostlist.HostSet
		AbsentRanks *system.RankSet
	}
)

// resolveRanks derives ranks to be used for fanout by comparing host and rank
// sets with the contents of the membership.
func (svc *ControlService) resolveRanks(hosts, ranks string) (hitRS, missRS *system.RankSet, missHS *hostlist.HostSet, err error) {
	hasHosts := hosts != ""
	hasRanks := ranks != ""

	if svc.membership == nil {
		err = errors.New("nil system membership")
		return
	}

	switch {
	case hasHosts && hasRanks:
		err = errors.New("ranklist and hostlist cannot both be set in request")
	case hasHosts:
		if hitRS, missHS, err = svc.membership.CheckHosts(hosts, svc.srvCfg.ControlPort,
			net.ResolveTCPAddr); err != nil {

			return
		}
		svc.log.Debugf("resolveRanks(): req hosts %s, hit ranks %s, miss hosts %s",
			hosts, hitRS, missHS)
	case hasRanks:
		if hitRS, missRS, err = svc.membership.CheckRanks(ranks); err != nil {
			return
		}
		svc.log.Debugf("resolveRanks(): req ranks %s, hit ranks %s, miss ranks %s",
			ranks, hitRS, missRS)
	default:
		// empty rank/host sets implies include all ranks so pass empty
		// string to CheckRanks()
		if hitRS, missRS, err = svc.membership.CheckRanks(""); err != nil {
			return
		}
	}

	if missHS == nil {
		missHS = new(hostlist.HostSet)
	}
	if missRS == nil {
		missRS = new(system.RankSet)
	}

	return
}

// rpcFanout sends requests to ranks in list on their respective host
// addresses through functions implementing UnaryInvoker.
//
// Required client method and any force flag in request are passed as part of
// fanoutRequest.
//
// The fan-out host and rank lists are resolved by calling resolveRanks().
//
// Pass true as last parameter to update member states on request failure.
//
// Fan-out is invoked by control API *Ranks functions.
func (svc *ControlService) rpcFanout(parent context.Context, fanReq fanoutRequest, updateOnFail bool) (*fanoutResponse, *system.RankSet, error) {
	if fanReq.Method == nil {
		return nil, nil, errors.New("fanout request with nil method")
	}

	// populate missing hosts/ranks in outer response and resolve active ranks
	hitRanks, missRanks, missHosts, err := svc.resolveRanks(fanReq.Hosts, fanReq.Ranks)
	if err != nil {
		return nil, nil, err
	}

	resp := &fanoutResponse{AbsentHosts: missHosts, AbsentRanks: missRanks}
	if hitRanks.Count() == 0 {
		return resp, hitRanks, nil
	}

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	ranksReq := &control.RanksReq{
		Ranks: hitRanks.String(), Force: fanReq.Force,
	}
	ranksReq.SetHostList(svc.membership.HostList(hitRanks))
	ranksResp, err := fanReq.Method(ctx, svc.rpcClient, ranksReq)
	if err != nil {
		return nil, nil, err
	}

	resp.Results = ranksResp.RankResults

	// synthesise "Stopped" rank results for any harness host errors
	hostRanks := svc.membership.HostRanks(hitRanks)
	for _, hes := range ranksResp.HostErrors {
		for _, addr := range strings.Split(hes.HostSet.DerangedString(), ",") {
			for _, rank := range hostRanks[addr] {
				resp.Results = append(resp.Results,
					&system.MemberResult{
						Rank: rank, Msg: hes.HostError.Error(),
						State: system.MemberStateUnresponsive,
					})
			}
			svc.log.Debugf("harness %s (ranks %v) host error: %s",
				addr, hostRanks[addr], hes.HostError)
		}
	}

	if len(resp.Results) != hitRanks.Count() {
		svc.log.Debugf("expected %d results, got %d",
			hitRanks.Count(), len(resp.Results))
	}

	if err = svc.membership.UpdateMemberStates(resp.Results, updateOnFail); err != nil {
		return nil, nil, err
	}

	return resp, hitRanks, nil
}

// SystemQuery implements the method defined for the Management Service.
//
// Return status of system members specified in request rank list (or all
// members if request rank list is empty).
//
// Request harnesses to ping their instances (system members) to determine
// IO Server process responsiveness. Update membership appropriately.
//
// This control service method is triggered from the control API method of the
// same name in lib/control/system.go and returns results from all selected ranks.
func (svc *ControlService) SystemQuery(ctx context.Context, pbReq *ctlpb.SystemQueryReq) (*ctlpb.SystemQueryResp, error) {
	svc.log.Debug("Received SystemQuery RPC")

	if pbReq == nil {
		return nil, errors.Errorf("nil %T request", pbReq)
	}

	fanResp, rankSet, err := svc.rpcFanout(ctx, fanoutRequest{
		Method: control.PingRanks,
		Hosts:  pbReq.GetHosts(),
		Ranks:  pbReq.GetRanks(),
	}, true)
	if err != nil {
		return nil, err
	}
	pbResp := &ctlpb.SystemQueryResp{
		Absentranks: fanResp.AbsentRanks.String(),
		Absenthosts: fanResp.AbsentHosts.String(),
	}

	if rankSet.Count() > 0 {
		members := svc.membership.Members(rankSet)
		if err := convert.Types(members, &pbResp.Members); err != nil {
			return nil, err
		}
	}

	svc.log.Debugf("Responding to SystemQuery RPC: %+v", pbResp)

	return pbResp, nil
}

func populateStopResp(fanResp *fanoutResponse, pbResp *ctlpb.SystemStopResp, action string) error {
	pbResp.Absentranks = fanResp.AbsentRanks.String()
	pbResp.Absenthosts = fanResp.AbsentHosts.String()

	if err := convert.Types(fanResp.Results, &pbResp.Results); err != nil {
		return err
	}
	for _, result := range pbResp.Results {
		result.Action = action
	}

	return nil
}

// SystemStop implements the method defined for the Management Service.
//
// Initiate two-phase controlled shutdown of DAOS system, return results for
// each selected rank. First phase results in "PrepShutdown" dRPC requests being
// issued to each rank and the second phase stops the running executable
// processes associated with each rank.
//
// This control service method is triggered from the control API method of the
// same name in lib/control/system.go and returns results from all selected ranks.
func (svc *ControlService) SystemStop(ctx context.Context, pbReq *ctlpb.SystemStopReq) (*ctlpb.SystemStopResp, error) {
	svc.log.Debug("Received SystemStop RPC")

	if pbReq == nil {
		return nil, errors.Errorf("nil %T request", pbReq)
	}

	// TODO: consider locking to prevent join attempts when shutting down
	pbResp := new(ctlpb.SystemStopResp)

	fanReq := fanoutRequest{
		Hosts: pbReq.GetHosts(),
		Ranks: pbReq.GetRanks(),
		Force: pbReq.GetForce(),
	}

	if pbReq.GetPrep() {
		svc.log.Debug("prepping ranks for shutdown")

		fanReq.Method = control.PrepShutdownRanks
		fanResp, _, err := svc.rpcFanout(ctx, fanReq, false)
		if err != nil {
			return nil, err
		}
		if err := populateStopResp(fanResp, pbResp, "prep shutdown"); err != nil {
			return nil, err
		}
		if !fanReq.Force && fanResp.Results.HasErrors() {
			return pbResp, errors.New("PrepShutdown HasErrors")
		}
	}
	if pbReq.GetKill() {
		svc.log.Debug("shutting down ranks")

		fanReq.Method = control.StopRanks
		fanResp, _, err := svc.rpcFanout(ctx, fanReq, false)
		if err != nil {
			return nil, err
		}
		if err := populateStopResp(fanResp, pbResp, "stop"); err != nil {
			return nil, err
		}
	}

	if pbResp.GetResults() == nil {
		return nil, errors.New("response results not populated")
	}

	svc.log.Debugf("Responding to SystemStop RPC: %+v", pbResp)

	return pbResp, nil
}

// SystemStart implements the method defined for the Management Service.
//
// Initiate controlled start of DAOS system instances (system members)
// after a controlled shutdown using information in the membership registry.
// Return system start results.
//
// This control service method is triggered from the control API method of the
// same name in lib/control/system.go and returns results from all selected ranks.
func (svc *ControlService) SystemStart(ctx context.Context, pbReq *ctlpb.SystemStartReq) (*ctlpb.SystemStartResp, error) {
	svc.log.Debug("Received SystemStart RPC")

	if pbReq == nil {
		return nil, errors.Errorf("nil %T request", pbReq)
	}

	fanResp, _, err := svc.rpcFanout(ctx, fanoutRequest{
		Method: control.StartRanks,
		Hosts:  pbReq.GetHosts(),
		Ranks:  pbReq.GetRanks(),
	}, false)
	if err != nil {
		return nil, err
	}

	pbResp := &ctlpb.SystemStartResp{
		Absentranks: fanResp.AbsentRanks.String(),
		Absenthosts: fanResp.AbsentHosts.String(),
	}
	if err := convert.Types(fanResp.Results, &pbResp.Results); err != nil {
		return nil, err
	}
	for _, result := range pbResp.Results {
		result.Action = "start"
	}

	svc.log.Debugf("Responding to SystemStart RPC: %+v", pbResp)

	return pbResp, nil
}

// SystemResetFormat implements the method defined for the Management Service.
//
// Prepare to reformat DAOS system by resetting format state of each rank
// and await storage format on each relevant instance (system member).
//
// This control service method is triggered from the control API method of the
// same name in lib/control/system.go and returns results from all selected ranks.
func (svc *ControlService) SystemResetFormat(ctx context.Context, pbReq *ctlpb.SystemResetFormatReq) (*ctlpb.SystemResetFormatResp, error) {
	svc.log.Debug("Received SystemResetFormat RPC")

	if pbReq == nil {
		return nil, errors.Errorf("nil %T request", pbReq)
	}

	fanResp, _, err := svc.rpcFanout(ctx, fanoutRequest{
		Method: control.ResetFormatRanks,
		Hosts:  pbReq.GetHosts(),
		Ranks:  pbReq.GetRanks(),
	}, false)
	if err != nil {
		return nil, err
	}

	pbResp := &ctlpb.SystemResetFormatResp{
		Absentranks: fanResp.AbsentRanks.String(),
		Absenthosts: fanResp.AbsentHosts.String(),
	}
	if err := convert.Types(fanResp.Results, &pbResp.Results); err != nil {
		return nil, err
	}
	for _, result := range pbResp.Results {
		result.Action = "reset format"
	}

	svc.log.Debugf("Responding to SystemResetFormat RPC: %+v", pbResp)

	return pbResp, nil
}
