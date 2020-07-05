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
	"fmt"
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
	case hasRanks:
		if hitRS, missRS, err = svc.membership.CheckRanks(ranks); err != nil {
			return
		}
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

// unpackSysReq takes the protobuf request, selects on its type and returns the
// client call object and force flag if set in request.
func unpackSysReq(pbReq interface{}) (_ systemRanksFunc, _ bool, _ string, _ string, err error) {
	if pbReq == nil {
		return nil, false, "", "", errors.New("nil request")
	}

	switch v := pbReq.(type) {
	case *ctlpb.SystemQueryReq:
		if v == nil {
			err = errors.Errorf("nil %T request", v)
			return
		}
		return control.PingRanks, false, v.GetHosts(), v.GetRanks(), nil
	case *ctlpb.SystemPrepShutdownReq:
		fmt.Printf("processing prepare progress\n")
		if v == nil {
			err = errors.Errorf("nil %T request", v)
			return
		}
		return control.PrepShutdownRanks, false, v.GetHosts(), v.GetRanks(), nil
	case *ctlpb.SystemStopReq:
		if v == nil {
			err = errors.Errorf("nil %T request", v)
			return
		}
		return control.StopRanks, v.GetForce(), v.GetHosts(), v.GetRanks(), nil
	case *ctlpb.SystemResetFormatReq:
		if v == nil {
			err = errors.Errorf("nil %T request", v)
			return
		}
		return control.ResetFormatRanks, false, v.GetHosts(), v.GetRanks(), nil
	case *ctlpb.SystemStartReq:
		if v == nil {
			err = errors.Errorf("nil %T request", v)
			return
		}
		return control.StartRanks, false, v.GetHosts(), v.GetRanks(), nil
	default:
		err = errors.Errorf("unknown request type: %T", v)
		return
	}
}

// rpcFanout sends requests to ranks in list on their respective host
// addresses through functions implementing UnaryInvoker.
//
// Required client method and any force flag in request are returned from
// processSysReq().
//
// The fan-out host and rank lists are resolved by calling resolveRanks().
//
// Pass true as last parameter to update member states on request failure.
//
// Fan-out is invoked by control API *Ranks functions.
func (svc *ControlService) rpcFanout(parent context.Context, pbReq interface{}, updateOnFail bool) (*fanoutResponse, *system.RankSet, error) {
	// derive fanout call and extract force flag, hosts and ranks from pbReq
	method, force, hosts, ranks, err := unpackSysReq(pbReq)
	if err != nil {
		return nil, nil, err
	}

	// populate missing hosts/ranks in outer response and resolve active ranks
	presentRanks, absentRanks, absentHosts, err := svc.resolveRanks(hosts, ranks)
	if err != nil {
		return nil, nil, err
	}
	resp := &fanoutResponse{AbsentHosts: absentHosts, AbsentRanks: absentRanks}
	if presentRanks.Count() == 0 {
		return resp, presentRanks, nil
	}

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	ranksReq := &control.RanksReq{
		Ranks: presentRanks.String(), Force: force,
	}
	ranksReq.SetHostList(svc.membership.HostList(presentRanks))
	ranksResp, err := method(ctx, svc.rpcClient, ranksReq)
	if err != nil {
		return nil, nil, err
	}

	resp.Results = ranksResp.RankResults

	// synthesise "Stopped" rank results for any harness host errors
	hostRanks := svc.membership.HostRanks(presentRanks)
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

	if len(resp.Results) != presentRanks.Count() {
		svc.log.Debugf("expected %d results, got %d",
			presentRanks.Count(), len(resp.Results))
	}

	if err = svc.membership.UpdateMemberStates(resp.Results, updateOnFail); err != nil {
		return nil, nil, err
	}

	return resp, presentRanks, nil
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

	fanResp, rankSet, err := svc.rpcFanout(ctx, pbReq, true)
	if err != nil {
		return nil, err
	}

	members := svc.membership.Members(rankSet)

	pbResp := &ctlpb.SystemQueryResp{
		Absentranks: fanResp.AbsentRanks.String(),
		Absenthosts: fanResp.AbsentHosts.String(),
	}
	if err := convert.Types(members, &pbResp.Members); err != nil {
		return nil, err
	}

	svc.log.Debug("Responding to SystemQuery RPC")

	return pbResp, nil
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

	// TODO: consider locking to prevent join attempts when shutting down
	pbResp := new(ctlpb.SystemStopResp)

	if pbReq.GetPrep() {
		svc.log.Debug("preparing ranks for shutdown")

		pbPrepReq := new(ctlpb.SystemPrepShutdownReq)
		if err := convert.Types(pbReq, pbPrepReq); err != nil {
			return nil, err
		}
		fanResp, _, err := svc.rpcFanout(ctx, pbPrepReq, false)
		if err != nil {
			return nil, err
		}
		pbResp.Absentranks = fanResp.AbsentRanks.String()
		pbResp.Absenthosts = fanResp.AbsentHosts.String()
		if err = convert.Types(fanResp.Results, &pbResp.Results); err != nil {
			return nil, err
		}
		for _, result := range pbResp.Results {
			result.Action = "prep shutdown"
		}

		if !pbReq.Force && fanResp.Results.HasErrors() {
			return pbResp, errors.New("PrepShutdown HasErrors")
		}
	}

	if pbReq.GetKill() {
		svc.log.Debug("shutting down ranks")

		fanResp, _, err := svc.rpcFanout(ctx, pbReq, false)
		if err != nil {
			return nil, err
		}
		pbResp.Absentranks = fanResp.AbsentRanks.String()
		pbResp.Absenthosts = fanResp.AbsentHosts.String()
		if err = convert.Types(fanResp.Results, &pbResp.Results); err != nil {
			return nil, err
		}
		for _, result := range pbResp.Results {
			result.Action = "stop"
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
//
// This control service method is triggered from the control API method of the
// same name in lib/control/system.go and returns results from all selected ranks.
func (svc *ControlService) SystemStart(ctx context.Context, pbReq *ctlpb.SystemStartReq) (*ctlpb.SystemStartResp, error) {
	svc.log.Debug("Received SystemStart RPC")

	fanResp, _, err := svc.rpcFanout(ctx, pbReq, false)
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

	svc.log.Debug("Responding to SystemStart RPC")

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

	fanResp, _, err := svc.rpcFanout(ctx, pbReq, false)
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

	svc.log.Debug("Responding to SystemResetFormat RPC")

	return pbResp, nil
}
