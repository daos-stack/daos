//
// (C) Copyright 2020 Intel Corporation.
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
	"context"
	"fmt"
	"net"
	"syscall"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/system"
)

const instanceUpdateDelay = 500 * time.Millisecond

// NewRankResult returns a reference to a new member result struct.
func NewRankResult(rank system.Rank, action string, state system.MemberState, err error) *mgmtpb.RanksResp_RankResult {
	result := mgmtpb.RanksResp_RankResult{
		Rank: rank.Uint32(), Action: action, State: uint32(state),
	}
	if err != nil {
		result.Errored = true
		result.Msg = err.Error()
	}

	return &result
}

// drespToRankResult converts drpc.Response to proto.RanksResp_RankResult
//
// RankResult is populated with rank, state and error dependent on processing
// dRPC response. Target state param is populated on success, Errored otherwise.
func drespToRankResult(rank system.Rank, action string, dresp *drpc.Response, err error, tState system.MemberState) *mgmtpb.RanksResp_RankResult {
	var outErr error
	state := system.MemberStateErrored

	if err != nil {
		outErr = errors.WithMessagef(err, "rank %s dRPC failed", &rank)
	} else {
		resp := &mgmtpb.DaosResp{}
		if err = proto.Unmarshal(dresp.Body, resp); err != nil {
			outErr = errors.WithMessagef(err, "rank %s dRPC unmarshal failed",
				&rank)
		} else if resp.GetStatus() != 0 {
			outErr = errors.Errorf("rank %s dRPC returned DER %d",
				&rank, resp.GetStatus())
		}
	}

	if outErr == nil {
		state = tState
	}

	return NewRankResult(rank, action, state, outErr)
}

// getPeerListenAddr combines peer ip from supplied context with input port.
func getPeerListenAddr(ctx context.Context, listenAddrStr string) (net.Addr, error) {
	p, ok := peer.FromContext(ctx)
	if !ok {
		return nil, errors.New("peer details not found in context")
	}

	tcpAddr, ok := p.Addr.(*net.TCPAddr)
	if !ok {
		return nil, errors.Errorf("peer address (%s) not tcp", p.Addr)
	}

	// what port is the input address listening on?
	_, portStr, err := net.SplitHostPort(listenAddrStr)
	if err != nil {
		return nil, errors.Wrap(err, "get listening port")
	}

	// resolve combined IP/port address
	return net.ResolveTCPAddr(p.Addr.Network(),
		net.JoinHostPort(tcpAddr.IP.String(), portStr))
}

func (svc *mgmtSvc) Join(ctx context.Context, req *mgmtpb.JoinReq) (*mgmtpb.JoinResp, error) {
	// combine peer (sender) IP (from context) with listening port (from
	// joining instance's host addr, in request params) as addr to reply to.
	replyAddr, err := getPeerListenAddr(ctx, req.GetAddr())
	if err != nil {
		return nil, errors.WithMessage(err,
			"combining peer addr with listener port")
	}

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodJoin, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.JoinResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal Join response")
	}

	// if join successful, record membership
	if resp.GetStatus() == 0 {
		newState := system.MemberStateEvicted
		if resp.GetState() == mgmtpb.JoinResp_IN {
			newState = system.MemberStateStarted
		}

		member := system.NewMember(system.Rank(resp.GetRank()), req.GetUuid(), replyAddr, newState)

		created, oldState := svc.membership.AddOrUpdate(member)
		if created {
			svc.log.Debugf("new system member: rank %d, addr %s",
				resp.GetRank(), replyAddr)
		} else {
			svc.log.Debugf("updated system member: rank %d, addr %s, %s->%s",
				member.Rank, replyAddr, *oldState, newState)
			if *oldState == newState {
				svc.log.Errorf("unexpected same state in rank %d update (%s->%s)",
					member.Rank, *oldState, newState)
			}
		}
	}

	return resp, nil
}

// PrepShutdown implements the method defined for the Management Service.
//
// Prepare data-plane instance managed by control-plane for a controlled shutdown,
// identified by unique rank.
//
// Iterate over instances, issuing PrepShutdown dRPCs and record results.
// Return error in addition to response if any instance requests not successful
// so retries can be performed at sender.
func (svc *mgmtSvc) PrepShutdownRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PrepShutdown dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	for _, i := range svc.harness.Instances() {
		rank, err := i.GetRank()
		if err != nil {
			return nil, err
		}

		if !rank.InList(system.RanksFromUint32(req.GetRanks())) {
			continue // filtered out, no result expected
		}

		if !i.isReady() {
			resp.Results = append(resp.Results,
				NewRankResult(rank, "prep shutdown",
					system.MemberStateStopped, nil))
			continue
		}

		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodPrepShutdown, nil)

		resp.Results = append(resp.Results,
			drespToRankResult(rank, "prep shutdown", dresp, err,
				system.MemberStateStopping))
	}

	svc.log.Debugf("MgmtSvc.PrepShutdown dispatch, resp:%+v\n", *resp)

	return resp, nil
}

func (svc *mgmtSvc) getStartedResults(rankList []system.Rank, desiredState system.MemberState, action string, stopErrs map[system.Rank]error) (system.MemberResults, error) {
	results := make(system.MemberResults, 0, maxIOServers)
	for _, i := range svc.harness.Instances() {
		rank, err := i.GetRank()
		if err != nil {
			return nil, err
		}

		if !rank.InList(rankList) {
			continue // filtered out, no result expected
		}

		state := system.MemberStateStarted
		if !i.isReady() {
			state = system.MemberStateStopped
		}

		var extraErrMsg string
		if len(stopErrs) > 0 {
			if stopErr, exists := stopErrs[rank]; exists {
				if stopErr == nil {
					return nil, errors.New("expected non-nil error in error map")
				}
				extraErrMsg = fmt.Sprintf(" (%s)", stopErr.Error())
			}
		}
		if state != desiredState {
			err = errors.Errorf("want %s, got %s%s", desiredState, state, extraErrMsg)
		}

		results = append(results, system.NewMemberResult(rank, action, err, state))
	}

	return results, nil
}

// StopRanks implements the method defined for the Management Service.
//
// Stop data-plane instance managed by control-plane identified by unique rank.
// After attempting to stop instances through harness (when either all instances
// are stopped or timeout has occurred, populate response results based on
// instance started state.
func (svc *mgmtSvc) StopRanks(parent context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.StopRanks dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	rankList := system.RanksFromUint32(req.GetRanks())

	signal := syscall.SIGINT
	if req.Force {
		signal = syscall.SIGKILL
	}

	ctx, cancel := context.WithTimeout(parent, svc.harness.rankReqTimeout)
	defer cancel()

	stopErrs, err := svc.harness.StopInstances(ctx, signal, rankList...)
	if err != nil {
		if err != context.DeadlineExceeded {
			// unexpected error, fail without collecting rank results
			return nil, err
		}
	}

	stopped := make(chan struct{})
	// poll until instances in rank list stop or timeout occurs
	// (at which point get results of each instance)
	go func() {
		for {
			success := true
			for _, rank := range rankList {
				if rank.InList(svc.harness.startedRanks()) {
					success = false
				}
			}
			if !success {
				time.Sleep(instanceUpdateDelay)
				continue
			}
			close(stopped)
			break
		}
	}()

	select {
	case <-stopped:
	case <-time.After(svc.harness.rankReqTimeout):
		svc.log.Debug("deadline exceeded when waiting for instances to stop")
	}

	results, err := svc.getStartedResults(rankList, system.MemberStateStopped, "stop", stopErrs)
	if err != nil {
		return nil, err
	}

	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.StopRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

func ping(i *IOServerInstance, rank system.Rank, timeout time.Duration) *mgmtpb.RanksResp_RankResult {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	resChan := make(chan *mgmtpb.RanksResp_RankResult)
	go func() {
		var err error
		var dresp *drpc.Response

		dresp, err = i.CallDrpc(drpc.ModuleMgmt, drpc.MethodPingRank, nil)

		select {
		case <-ctx.Done():
		case resChan <- drespToRankResult(rank, "ping", dresp, err, system.MemberStateStarted):
		}
	}()

	select {
	case result := <-resChan:
		return result
	case <-time.After(timeout):
		return NewRankResult(rank, "ping", system.MemberStateUnresponsive,
			errors.New("timeout occurred"))
	}
}

// PingRanks implements the method defined for the Management Service.
//
// Query data-plane all instances (DAOS system members) managed by harness to verify
// responsiveness.
//
// For each instance, call over dRPC and either return error for CallDrpc err or
// populate a RanksResp_RankResult in response. Result is either populated from
// return from dRPC which indicates activity and Status == 0, or in the case of a timeout
// the results status will be pingTimeoutStatus.
func (svc *mgmtSvc) PingRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PingRanks dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	for _, i := range svc.harness.Instances() {
		rank, err := i.GetRank()
		if err != nil {
			return nil, err
		}

		if !rank.InList(system.RanksFromUint32(req.GetRanks())) {
			continue // filtered out, no result expected
		}

		if !i.isReady() {
			resp.Results = append(resp.Results,
				NewRankResult(rank, "ping", system.MemberStateStopped, nil))
			continue
		}

		resp.Results = append(resp.Results, ping(i, rank, svc.harness.rankReqTimeout))
	}

	svc.log.Debugf("MgmtSvc.PingRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// StartRanks implements the method defined for the Management Service.
//
// Restart data-plane instances (DAOS system members) managed by harness.
func (svc *mgmtSvc) StartRanks(parent context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.StartRanks dispatch, req:%+v\n", *req)

	rankList := system.RanksFromUint32(req.GetRanks())

	if err := svc.harness.StartInstances(rankList); err != nil {
		return nil, err
	}

	started := make(chan struct{})
	// select until instances in rank list start or timeout occurs
	// (at which point get results of each instance)
	go func() {
		for {
			success := true
			for _, rank := range rankList {
				if !rank.InList(svc.harness.readyRanks()) {
					success = false
				}
			}
			if !success {
				time.Sleep(instanceUpdateDelay)
				continue
			}
			close(started)
			break
		}
	}()

	select {
	case <-started:
	case <-time.After(svc.harness.rankStartTimeout):
	}

	results, err := svc.getStartedResults(rankList, system.MemberStateStarted, "start", nil)
	if err != nil {
		return nil, err
	}
	resp := &mgmtpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.StartRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// LeaderQuery returns the system leader and access point replica details.
func (svc *mgmtSvc) LeaderQuery(ctx context.Context, req *mgmtpb.LeaderQueryReq) (*mgmtpb.LeaderQueryResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	if len(svc.harness.instances) == 0 {
		return nil, errors.New("no I/O servers configured; can't determine leader")
	}

	instance := svc.harness.instances[0]
	sb := instance.getSuperblock()
	if sb == nil {
		return nil, errors.New("no I/O superblock found; can't determine leader")
	}

	if req.System != sb.System {
		return nil, errors.Errorf("received leader query for wrong system (local: %q, req: %q)",
			sb.System, req.System)
	}

	leaderAddr, err := instance.msClient.LeaderAddress()
	if err != nil {
		return nil, errors.Wrap(err, "failed to determine current leader address")
	}

	return &mgmtpb.LeaderQueryResp{
		CurrentLeader: leaderAddr,
		Replicas:      instance.msClient.cfg.AccessPoints,
	}, nil
}
