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
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	instanceUpdateDelay = 500 * time.Millisecond

	batchJoinInterval = 250 * time.Millisecond
	joinRespTimeout   = 10 * time.Millisecond
)

type (
	batchJoinRequest struct {
		mgmtpb.JoinReq
		peerAddr *net.TCPAddr
		respCh   chan *batchJoinResponse
	}

	batchJoinResponse struct {
		mgmtpb.JoinResp
		joinErr error
	}

	joinReqChan chan *batchJoinRequest
)

// pollInstanceState waits for either context to be cancelled/timeout or for the
// provided validate function to return true for each of the provided instances.
//
// Returns true if all instances return true from the validate function within
// the given timeout, false otherwise. Error is returned if parent context is
// cancelled or times out.
func pollInstanceState(ctx context.Context, instances []*IOServerInstance, validate func(*IOServerInstance) bool, timeout time.Duration) (bool, error) {
	ready := make(chan struct{})
	go func() {
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}

			success := true
			for _, srv := range instances {
				if !validate(srv) {
					success = false
				}
			}
			if success {
				close(ready)
				return
			}
			time.Sleep(instanceUpdateDelay)
		}
	}()

	select {
	case <-ctx.Done():
		return false, ctx.Err()
	case <-time.After(timeout):
		return false, nil
	case <-ready:
		return true, nil
	}
}

// getPeerListenAddr combines peer ip from supplied context with input port.
func getPeerListenAddr(ctx context.Context, listenAddrStr string) (*net.TCPAddr, error) {
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

func (svc *mgmtSvc) startJoinLoop(ctx context.Context) {
	svc.log.Debug("starting joinLoop")
	go svc.joinLoop(ctx)
}

func (svc *mgmtSvc) joinLoop(parent context.Context) {
	var joinReqs []*batchJoinRequest

	for {
		select {
		case <-parent.Done():
			svc.log.Debug("stopped joinLoop")
			return
		case jr := <-svc.joinReqs:
			joinReqs = append(joinReqs, jr)
		case <-time.After(batchJoinInterval):
			if len(joinReqs) == 0 {
				continue
			}

			svc.log.Debugf("processing %d join requests", len(joinReqs))
			joinResps := make([]*batchJoinResponse, len(joinReqs))
			for i, req := range joinReqs {
				joinResps[i] = svc.join(parent, req)
			}

			for i := 0; i < len(svc.harness.Instances()); i++ {
				if err := svc.doGroupUpdate(parent); err != nil {
					if err == instanceNotReady {
						svc.log.Debug("group update not ready (retrying)")
						continue
					}

					err = errors.Wrap(err, "failed to perform CaRT group update")
					for i, jr := range joinResps {
						if jr.joinErr == nil {
							joinResps[i] = &batchJoinResponse{joinErr: err}
						}
					}
				}
				break
			}

			svc.log.Debugf("sending %d join responses", len(joinReqs))
			for i, req := range joinReqs {
				ctx, cancel := context.WithTimeout(parent, joinRespTimeout)
				defer cancel()

				select {
				case <-ctx.Done():
					svc.log.Errorf("failed to send join response: %s", ctx.Err())
				case req.respCh <- joinResps[i]:
				}
			}

			joinReqs = nil
		}
	}
}

func (svc *mgmtSvc) join(ctx context.Context, req *batchJoinRequest) *batchJoinResponse {
	uuid, err := uuid.Parse(req.GetUuid())
	if err != nil {
		return &batchJoinResponse{
			joinErr: errors.Wrapf(err, "invalid uuid %q", req.GetUuid()),
		}
	}

	fd, err := system.NewFaultDomainFromString(req.GetSrvFaultDomain())
	if err != nil {
		return &batchJoinResponse{
			joinErr: errors.Wrapf(err, "invalid server fault domain %q", req.GetSrvFaultDomain()),
		}
	}

	joinResponse, err := svc.membership.Join(&system.JoinRequest{
		Rank:           system.Rank(req.Rank),
		UUID:           uuid,
		ControlAddr:    req.peerAddr,
		FabricURI:      req.GetUri(),
		FabricContexts: req.GetNctxs(),
		FaultDomain:    fd,
	})
	if err != nil {
		return &batchJoinResponse{joinErr: err}
	}

	member := joinResponse.Member
	if joinResponse.Created {
		svc.log.Debugf("new system member: rank %d, addr %s, uri %s",
			member.Rank, req.peerAddr, member.FabricURI)
	} else {
		svc.log.Debugf("updated system member: rank %d, uri %s, %s->%s",
			member.Rank, member.FabricURI, joinResponse.PrevState, member.State())
		if joinResponse.PrevState == member.State() {
			svc.log.Errorf("unexpected same state in rank %d update (%s->%s)",
				member.Rank, joinResponse.PrevState, member.State())
		}
	}

	resp := &batchJoinResponse{
		JoinResp: mgmtpb.JoinResp{
			State: mgmtpb.JoinResp_IN,
			Rank:  member.Rank.Uint32(),
		},
	}

	// If the rank is local to the MS leader, then we need to wire up at least
	// one in order to perform a CaRT group update.
	if common.IsLocalAddr(req.peerAddr) && req.Idx == 0 {
		resp.LocalJoin = true

		srvs := svc.harness.Instances()
		if len(srvs) == 0 {
			return &batchJoinResponse{
				joinErr: errors.New("invalid Join request (index 0 doesn't exist?!?)"),
			}
		}
		srv := srvs[0]

		if err := srv.callSetRank(ctx, joinResponse.Member.Rank); err != nil {
			return &batchJoinResponse{
				joinErr: errors.Wrap(err, "failed to set rank on local instance"),
			}
		}

		if err := srv.callSetUp(ctx); err != nil {
			return &batchJoinResponse{
				joinErr: errors.Wrap(err, "failed to load local instance modules"),
			}
		}

		// mark the ioserver as ready to handle dRPC requests
		srv.ready.SetTrue()
	}

	return resp
}

func (svc *mgmtSvc) doGroupUpdate(ctx context.Context) error {
	gm, err := svc.sysdb.GroupMap()
	if err != nil {
		return err
	}
	if len(gm.RankURIs) == 0 {
		return system.ErrEmptyGroupMap
	}

	req := &mgmtpb.GroupUpdateReq{
		MapVersion: gm.Version,
	}
	rankSet := &system.RankSet{}
	for rank, uri := range gm.RankURIs {
		req.Servers = append(req.Servers, &mgmtpb.GroupUpdateReq_Server{
			Rank: rank.Uint32(),
			Uri:  uri,
		})
		rankSet.Add(rank)
	}

	svc.log.Debugf("group update request: version: %d, ranks: %s", req.MapVersion, rankSet)
	dResp, err := svc.harness.CallDrpc(ctx, drpc.MethodGroupUpdate, req)
	if err != nil {
		if err == instanceNotReady {
			return err
		}
		svc.log.Errorf("dRPC GroupUpdate call failed: %s", err)
		return err
	}

	resp := new(mgmtpb.GroupUpdateResp)
	if err = proto.Unmarshal(dResp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal GroupUpdate response")
	}

	if resp.GetStatus() != 0 {
		return drpc.DaosStatus(resp.GetStatus())
	}
	return nil
}

// Join management service gRPC handler receives Join requests from
// control-plane instances attempting to register a managed instance (will be a
// rank once joined) to the DAOS system.
//
// On receipt of the join request, add to a queue of requests to be processed
// periodically in a dedicated goroutine. This architecture provides for thread
// safety and improved performance while updating the system membership and CaRT
// primary group in the local ioserver.
//
// The state of the newly joined/evicted rank along with the reply address used
// to contact the new rank in future will be registered in the system membership.
// The reply address is generated by combining peer (sender) IP (from context)
// with listening port from joining instance's host addr contained in the
// provided request.
func (svc *mgmtSvc) Join(ctx context.Context, req *mgmtpb.JoinReq) (*mgmtpb.JoinResp, error) {
	svc.log.Debugf("MgmtSvc.Join dispatch, req:%#v\n", req)

	replyAddr, err := getPeerListenAddr(ctx, req.GetAddr())
	if err != nil {
		return nil, errors.WithMessage(err, "combining peer addr with listener port")
	}

	bjr := &batchJoinRequest{
		JoinReq:  *req,
		peerAddr: replyAddr,
		respCh:   make(chan *batchJoinResponse),
	}

	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case svc.joinReqs <- bjr:
	}

	var resp *mgmtpb.JoinResp
	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case r := <-bjr.respCh:
		if r.joinErr != nil {
			return nil, r.joinErr
		}
		resp = &r.JoinResp
	}

	svc.log.Debugf("MgmtSvc.Join dispatch, resp:%#v\n", resp)

	return resp, nil
}

// drpcOnLocalRanks iterates over local instances issuing dRPC requests in
// parallel and returning system member results when all have been received.
func (svc *mgmtSvc) drpcOnLocalRanks(parent context.Context, req *mgmtpb.RanksReq, method drpc.Method) ([]*system.MemberResult, error) {
	ctx, cancel := context.WithTimeout(parent, svc.harness.rankReqTimeout)
	defer cancel()

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}

	inflight := 0
	ch := make(chan *system.MemberResult)
	for _, srv := range instances {
		inflight++
		go func(s *IOServerInstance) {
			ch <- s.TryDrpc(ctx, method)
		}(srv)
	}

	results := make(system.MemberResults, 0, inflight)
	for inflight > 0 {
		result := <-ch
		inflight--
		if result == nil {
			return nil, errors.New("nil result")
		}
		results = append(results, result)
	}

	return results, nil
}

// PrepShutdown implements the method defined for the Management Service.
//
// Prepare data-plane instance(s) managed by control-plane for a controlled shutdown,
// identified by unique rank(s).
//
// Iterate over local instances, issuing PrepShutdown dRPCs and record results.
func (svc *mgmtSvc) PrepShutdownRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.PrepShutdownRanks dispatch, req:%+v\n", *req)

	results, err := svc.drpcOnLocalRanks(ctx, req, drpc.MethodPrepShutdown)
	if err != nil {
		return nil, errors.Wrap(err, "sending request over dRPC to local ranks")
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
func (svc *mgmtSvc) memberStateResults(instances []*IOServerInstance, desiredState system.MemberState, successMsg string) (system.MemberResults, error) {
	results := make(system.MemberResults, 0, len(instances))
	for _, srv := range instances {
		rank, err := srv.GetRank()
		if err != nil {
			svc.log.Debugf("Instance %d GetRank(): %s", srv.Index(), err)
			continue
		}

		state := srv.LocalState()
		if state != desiredState {
			results = append(results, system.NewMemberResult(rank,
				errors.Errorf("want %s, got %s", desiredState, state), state))
			continue
		}

		results = append(results, &system.MemberResult{
			Rank: rank, Msg: successMsg, State: state,
		})
	}

	return results, nil
}

// StopRanks implements the method defined for the Management Service.
//
// Stop data-plane instance(s) managed by control-plane identified by unique
// rank(s). After attempting to stop instances through harness (when either all
// instances are stopped or timeout has occurred), populate response results
// based on local instance state.
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

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
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

	// ignore poll results as we gather state immediately after
	if _, err = pollInstanceState(ctx, instances,
		func(s *IOServerInstance) bool { return !s.isStarted() },
		svc.harness.rankReqTimeout); err != nil {

		return nil, err
	}

	results, err := svc.memberStateResults(instances, system.MemberStateStopped, "system stop")
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
// Iterate over local instances, issuing async Ping dRPCs and record results.
func (svc *mgmtSvc) PingRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.PingRanks dispatch, req:%+v\n", *req)

	results, err := svc.drpcOnLocalRanks(ctx, req, drpc.MethodPingRank)
	if err != nil {
		return nil, errors.Wrap(err, "sending request over dRPC to local ranks")
	}

	resp := &mgmtpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PingRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// ResetFormatRanks implements the method defined for the Management Service.
//
// Reset storage format of data-plane instances (DAOS system members) managed
// by harness.
//
// Reset formatted state of data-plane instance(s) managed by control-plane
// identified by unique rank(s). After attempting to reset instances through
// harness (when either all instances are awaiting format or timeout has
// occurred), populate response results based on local instance state.
func (svc *mgmtSvc) ResetFormatRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.ResetFormatRanks dispatch, req:%+v\n", *req)

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}

	savedRanks := make(map[uint32]system.Rank) // instance idx to system rank
	for _, srv := range instances {
		rank, err := srv.GetRank()
		if err != nil {
			return nil, err
		}
		savedRanks[srv.Index()] = rank

		if srv.isStarted() {
			return nil, FaultInstancesNotStopped("reset format", rank)
		}
		if err := srv.RemoveSuperblock(); err != nil {
			return nil, err
		}
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case srv.startLoop <- true: // proceed to awaiting storage format
		}
	}

	// ignore poll results as we gather state immediately after
	if _, err = pollInstanceState(ctx, instances, (*IOServerInstance).isAwaitingFormat,
		svc.harness.rankStartTimeout); err != nil {

		return nil, err
	}

	// rank cannot be pulled from superblock so use saved value
	results := make(system.MemberResults, 0, len(instances))
	for _, srv := range instances {
		var err error
		state := srv.LocalState()
		if state != system.MemberStateAwaitFormat {
			err = errors.Errorf("want %s, got %s", system.MemberStateAwaitFormat, state)
		}

		results = append(results, system.NewMemberResult(savedRanks[srv.Index()], err, state))
	}

	resp := &mgmtpb.RanksResp{}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.ResetFormatRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// StartRanks implements the method defined for the Management Service.
//
// Start data-plane instance(s) managed by control-plane identified by unique
// rank(s). After attempting to start instances through harness (when either all
// instances are in ready state or timeout has occurred), populate response results
// based on local instance state.
func (svc *mgmtSvc) StartRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if len(req.GetRanks()) == 0 {
		return nil, errors.New("no ranks specified in request")
	}
	svc.log.Debugf("MgmtSvc.StartRanks dispatch, req:%+v\n", *req)

	instances, err := svc.harness.FilterInstancesByRankSet(req.GetRanks())
	if err != nil {
		return nil, err
	}
	for _, srv := range instances {
		if srv.isStarted() {
			continue
		}
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case srv.startLoop <- true:
		}
	}

	// ignore poll results as we gather state immediately after
	if _, err = pollInstanceState(ctx, instances, (*IOServerInstance).isReady,
		svc.harness.rankStartTimeout); err != nil {

		return nil, err
	}

	// instances will update state to "Started" through join or
	// bootstrap in membership, here just make sure instances are "Ready"
	results, err := svc.memberStateResults(instances, system.MemberStateReady, "system start")
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
