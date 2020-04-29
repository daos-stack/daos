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
			newState = system.MemberStateJoined
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

// localInstances takes slice of uint32 rank identifiers and returns a slice
// containing any local instances matching the supplied ranks.
func (svc *mgmtSvc) localInstances(inRanks []uint32) ([]*IOServerInstance, error) {
	localInstances := make([]*IOServerInstance, 0, maxIOServers)

	for _, rank := range system.RanksFromUint32(inRanks) {
		for _, instance := range svc.harness.Instances() {
			instanceRank, err := instance.GetRank()
			if err != nil {
				return nil, errors.WithMessage(err, "localInstances()")
			}
			if !rank.Equals(instanceRank) {
				continue // requested rank not local
			}
			localInstances = append(localInstances, instance)
		}
	}

	return localInstances, nil
}

// PrepShutdown implements the method defined for the Management Service.
//
// Prepare data-plane instance managed by control-plane for a controlled shutdown,
// identified by unique rank.
//
// Iterate over instances, issuing PrepShutdown dRPCs and record results.
// Return error in addition to response if any instance requests not successful
// so retries can be performed at sender.
func (svc *mgmtSvc) PrepShutdownRanks(parent context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.PrepShutdownRanks dispatch, req:%+v\n", *req)

	ctx, cancel := context.WithTimeout(parent, svc.harness.rankReqTimeout)
	defer cancel()

	instances, err := svc.localInstances(req.GetRanks())
	if err != nil {
		return nil, err
	}

	prepping := 0
	ch := make(chan *system.MemberResult)
	for _, srv := range instances {
		prepping++
		go func(s *IOServerInstance) {
			ch <- s.dPrepShutdown(ctx)
		}(srv)
	}

	results := make(system.MemberResults, 0, prepping)
	for prepping > 0 {
		result := <-ch
		prepping--
		if result == nil {
			return nil, errors.New("nil result")
		}
		results = append(results, result)
	}

	resp := &mgmtpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PrepShutdown dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// memberStateResults returns system member results reflecting whether the state
// of the given member is equivalent to the supplied desired state value.
func (svc *mgmtSvc) memberStateResults(instances []*IOServerInstance, desiredState system.MemberState, action string) (system.MemberResults, error) {
	results := make(system.MemberResults, 0, len(instances))
	for _, srv := range instances {
		state := system.MemberStateUnknown
		switch {
		case srv.isReady():
			state = system.MemberStateReady
		case srv.isStarted():
			state = system.MemberStateStarting
		case srv.waitStorage.IsTrue():
			state = system.MemberStateAwaitFormat
		default:
			state = system.MemberStateStopped
		}

		rank, err := srv.GetRank()
		if err != nil {
			return nil, err
		}

		if state != desiredState {
			err = errors.Errorf("want %s, got %s", desiredState, state)
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
func (svc *mgmtSvc) StopRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.StopRanks dispatch, req:%+v\n", *req)

	signal := syscall.SIGINT
	if req.Force {
		signal = syscall.SIGKILL
	}

	instances, err := svc.localInstances(req.GetRanks())
	if err != nil {
		return nil, err
	}
	for _, srv := range instances {
		if !srv.isStarted() {
			continue
		}
		if err := srv.Stop(signal); err != nil {
			return nil, errors.Wrapf(err, "sending %s", signal)
		}
	}

	stopped := make(chan struct{})
	// poll until instances in rank list stop or timeout occurs
	// (at which point get results of each instance)
	go func() {
		for {
			success := true
			for _, srv := range instances {
				if srv.isStarted() {
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
	case <-ctx.Done():
		return nil, ctx.Err()
	case <-stopped:
	case <-time.After(svc.harness.rankReqTimeout):
		svc.log.Debug("MgmtSvc.StopRanks rank stop timeout exceeded")
	}

	results, err := svc.memberStateResults(instances, system.MemberStateStopped, "stop")
	if err != nil {
		return nil, err
	}
	resp := &mgmtpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.StopRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PingRanks implements the method defined for the Management Service.
//
// Query data-plane all instances (DAOS system members) managed by harness to verify
// responsiveness.
//
// For each instance, call over dRPC with dPing() async and collect results.
func (svc *mgmtSvc) PingRanks(parent context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.PingRanks dispatch, req:%+v\n", *req)

	ctx, cancel := context.WithTimeout(parent, svc.harness.rankReqTimeout)
	defer cancel()

	instances, err := svc.localInstances(req.GetRanks())
	if err != nil {
		return nil, err
	}

	pinging := 0
	ch := make(chan *system.MemberResult)
	for _, srv := range instances {
		pinging++
		go func(s *IOServerInstance) {
			ch <- s.dPing(ctx)
		}(srv)
	}

	results := make(system.MemberResults, 0, pinging)
	for pinging > 0 {
		result := <-ch
		pinging--
		if result == nil {
			return nil, errors.New("nil result")
		}
		results = append(results, result)
	}

	resp := &mgmtpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PingRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// StartRanks implements the method defined for the Management Service.
//
// Restart data-plane instances (DAOS system members) managed by harness.
func (svc *mgmtSvc) StartRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.StartRanks dispatch, req:%+v\n", *req)

	instances, err := svc.localInstances(req.GetRanks())
	if err != nil {
		return nil, err
	}
	for _, srv := range instances {
		if srv.isStarted() {
			continue
		}
		srv.startLoop <- true
	}

	// instances will update state to "Started" through join or
	// bootstrap in membership, here just make sure instances "Ready"
	ready := make(chan struct{})
	go func() {
		for {
			success := true
			for _, srv := range instances {
				if !srv.isReady() {
					success = false
				}
			}
			if !success {
				time.Sleep(instanceUpdateDelay)
				continue
			}
			close(ready)
			break
		}
	}()

	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case <-ready:
	case <-time.After(svc.harness.rankStartTimeout):
		svc.log.Debug("MgmtSvc.StartRanks rank start timeout exceeded")
	}

	// want to make sure instance is reporting ready at minimum
	results, err := svc.memberStateResults(instances, system.MemberStateReady, "start")
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
