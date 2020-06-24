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
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/system"
)

const systemReqTimeout = 30 * time.Second

type (
	// systemRanksFunc is an alias for control client API *Ranks() fanout
	// function that executes across ranks on different hosts.
	systemRanksFunc func(context.Context, control.UnaryInvoker, *control.RanksReq) (*control.RanksResp, error)

	fanoutRequest struct {
		RankList string
		HostList string
		Force    bool
	}

	fanoutResponse struct {
		Results      system.MemberResults
		MissingRanks []system.Rank
		MissingHosts []string
	}
)

// any host that is managing a rank selected by the request rank list filter.

// resolveRanks derives ranks to be used for fanout by comparing requested host
// and rank lists with the contents of the membership.
func (svc *ControlService) resolveRanks(req *fanoutRequest) (*control.RanksReq, []system.Rank, []string, error) {
	var missingRanks []system.Rank
	var missingHosts []string
	ranksReq := new(control.RanksReq)
	ranksReq.Force = req.Force
	hasRanks := len(req.RankList) > 0
	hasHosts := len(req.HostList) > 0

	if svc.membership == nil {
		return nil, nil, nil, errors.New("nil system membership")
	}

	switch {
	case hasRanks && hasHosts:
		return nil, nil, nil, errors.New("ranklist and hostlist cannot both be set in request")
	case hasRanks:
		hit, miss, err := svc.membership.CheckRanklist(req.RankList)
		if err != nil {
			return nil, nil, nil, errors.Wrap(err, "ranks list")
		}
		ranksReq.Ranks = hit
		missingRanks = miss
	case hasHosts:
		// TODO: turn below logic into membership.CheckHostlist
		hostRanks := svc.membership.HostRanks()
		hs, err := hostlist.CreateSet(req.HostList)
		if err != nil {
			return nil, nil, nil, errors.Wrap(err, "rank-hosts list")
		}
		for _, host := range strings.Split(hs.DerangedString(), ",") {
			if ranks, exists := hostRanks[host]; exists {
				ranksReq.Ranks = append(ranksReq.Ranks, ranks...)
				continue
			}
			missingHosts = append(missingHosts, host)
		}
	default:
		// empty rank/host lists implies include all ranks
		ranksReq.Ranks = svc.membership.Ranks()
	}

	return ranksReq, missingRanks, missingHosts, nil
}

// processSysReq takes the protobuf request, selects on its type and returns to
// client call object along with populated fanoutReq to be used when calling.
func processSysReq(pbReq interface{}) (*fanoutRequest, systemRanksFunc, error) {
	if pbReq == nil {
		return nil, nil, errors.New("nil request")
	}

	fanReq := new(fanoutRequest)
	if err := convert.Types(pbReq, fanReq); err != nil {
		return nil, nil, err
	}

	var method systemRanksFunc
	switch pbReq.(type) {
	case *ctlpb.SystemQueryReq:
		method = control.PingRanks
		//	case *ctlpb.SystemPrepShutdownReq:
		//		method = control.PrepShutdownRanks
	case *ctlpb.SystemStopReq:
		method = control.StopRanks
	case *ctlpb.SystemResetFormatReq:
		method = control.ResetFormatRanks
	case *ctlpb.SystemStartReq:
		method = control.StartRanks
	default:
		return nil, nil, errors.New("unrecognised system ranks method")
	}

	return fanReq, method, nil
}

// rpcFanout sends requests to ranks in list on their respective host
// addresses through functions implementing UnaryInvoker.
//
// Required client method and converted request are returned from
// processSysReq().
//
// The fan-out host and rank lists are resolved by calling resolveRanks().
//
// Pass true as last parameter to update member states on request failure.
//
// Fan-out is invoked by control API *Ranks functions.
func (svc *ControlService) rpcFanout(parent context.Context, pbReq interface{}, updateOnFail bool) (resp *fanoutResponse, ranks []system.Rank, err error) {
	var (
		resolveReq *fanoutRequest
		method     systemRanksFunc
		missRanks  []system.Rank
		missHosts  []string
		fanReq     *control.RanksReq
		methResp   *control.RanksResp
	)

	resolveReq, method, err = processSysReq(pbReq)
	if err != nil {
		return
	}

	fanReq, missRanks, missHosts, err = svc.resolveRanks(resolveReq)
	if err != nil {
		return
	}

	resp = new(fanoutResponse)
	resp.MissingRanks = missRanks
	resp.MissingHosts = missHosts

	if len(fanReq.Ranks) == 0 {
		return
	}
	ranks = fanReq.Ranks

	ctx, cancel := context.WithTimeout(parent, systemReqTimeout)
	defer cancel()

	fanReq.SetHostList(svc.membership.Hosts(fanReq.Ranks...))
	methResp, err = method(ctx, svc.rpcClient, fanReq)
	if err != nil {
		return
	}

	resp.Results = methResp.RankResults

	// synthesise "Stopped" rank results for any harness host errors
	hostRanks := svc.membership.HostRanks(fanReq.Ranks...)
	for _, hes := range methResp.HostErrors {
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

	if len(resp.Results) != len(fanReq.Ranks) {
		svc.log.Debugf("expected %d results, got %d",
			len(fanReq.Ranks), len(resp.Results))
	}

	if err = svc.membership.UpdateMemberStates(resp.Results, !updateOnFail); err != nil {
		return
	}

	return
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

	_, ranks, err := svc.rpcFanout(ctx, pbReq, true)
	if err != nil {
		return nil, err
	}

	members := svc.membership.Members(ranks...)

	// TODO: add missing ranks to response
	// for _, rank := range fanoutResp.MissingRanks {
	// for _, rank := range fanoutResp.MissingHosts {

	pbResp := &ctlpb.SystemQueryResp{}
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

	pbResp := &ctlpb.SystemStopResp{}

	// TODO: consider locking to prevent join attempts when shutting down

	// TODO: fix selecting prep by sending relevant request in rpcFanout
	//	if pbReq.GetPrep() {
	//		svc.log.Debug("preparing ranks for shutdown")
	//
	//		fanResp, err := svc.rpcFanout(ctx, pbReq)
	//		if err != nil {
	//			return nil, err
	//		}
	//		if err = convert.Types(fanResp.Results, &pbResp.Results); err != nil {
	//			return nil, err
	//		}
	//		for _, result := range pbResp.Results {
	//			result.Action = "prep shutdown"
	//		}
	//		if !pbReq.Force && fanResp.Results.HasErrors() {
	//			return pbResp, errors.New("PrepShutdown HasErrors")
	//		}
	//	}

	if pbReq.GetKill() {
		svc.log.Debug("shutting down ranks")

		fanResp, _, err := svc.rpcFanout(ctx, pbReq, false)
		if err != nil {
			return nil, err
		}
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

	pbResp := &ctlpb.SystemStartResp{}
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

	pbResp := &ctlpb.SystemResetFormatResp{}
	if err := convert.Types(fanResp.Results, &pbResp.Results); err != nil {
		return nil, err
	}
	for _, result := range pbResp.Results {
		result.Action = "reset format"
	}

	svc.log.Debug("Responding to SystemResetFormat RPC")

	return pbResp, nil
}
