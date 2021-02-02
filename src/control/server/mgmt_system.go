//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"net"
	"strings"
	"time"

	"github.com/golang/protobuf/proto"
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/system"
)

// GetAttachInfo handles a request to retrieve a map of ranks to fabric URIs, in addition
// to client network autoconfiguration hints.
//
// The default use case, where req.AllRanks is false, is for libdaos clients to obtain
// the client network autoconfiguration hints, and the set of ranks associated with MS
// replicas. If req.AllRanks is true, all ranks' fabric URIs are also given the client.
func (svc *mgmtSvc) GetAttachInfo(ctx context.Context, req *mgmtpb.GetAttachInfoReq) (*mgmtpb.GetAttachInfoResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	if svc.clientNetworkCfg == nil {
		return nil, errors.New("clientNetworkCfg is missing")
	}
	svc.log.Debugf("MgmtSvc.GetAttachInfo dispatch, req:%+v\n", *req)

	groupMap, err := svc.sysdb.GroupMap()
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.GetAttachInfoResp)
	if req.GetAllRanks() {
		for rank, uri := range groupMap.RankURIs {
			resp.RankUris = append(resp.RankUris, &mgmtpb.GetAttachInfoResp_RankUri{
				Rank: rank.Uint32(),
				Uri:  uri,
			})
		}
	}
	resp.Provider = svc.clientNetworkCfg.Provider
	resp.CrtCtxShareAddr = svc.clientNetworkCfg.CrtCtxShareAddr
	resp.CrtTimeout = svc.clientNetworkCfg.CrtTimeout
	resp.NetDevClass = svc.clientNetworkCfg.NetDevClass
	resp.MsRanks = system.RanksToUint32(groupMap.MSRanks)

	// For resp.RankUris may be large, we make a resp copy with a limited
	// number of rank URIs, to avoid flooding the debug log.
	svc.log.Debugf("MgmtSvc.GetAttachInfo dispatch, resp:%+v len(RankUris):%d\n",
		*func(r *mgmtpb.GetAttachInfoResp) *mgmtpb.GetAttachInfoResp {
			max := 1
			if len(r.RankUris) <= max {
				return r
			}
			s := *r
			s.RankUris = s.RankUris[0:max]
			return &s
		}(resp), len(resp.RankUris))

	return resp, nil
}

// LeaderQuery returns the system leader and access point replica details.
func (svc *mgmtSvc) LeaderQuery(ctx context.Context, req *mgmtpb.LeaderQueryReq) (*mgmtpb.LeaderQueryResp, error) {
	if err := svc.checkSystemRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.LeaderQuery dispatch, req:%+v\n", req)

	leaderAddr, replicas, err := svc.sysdb.LeaderQuery()
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.LeaderQueryResp{
		CurrentLeader: leaderAddr,
		Replicas:      replicas,
	}

	svc.log.Debugf("MgmtSvc.LeaderQuery dispatch, resp:%+v\n", resp)
	return resp, nil
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

const (
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
					if err == errInstanceNotReady {
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
		if err == errInstanceNotReady {
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
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.Join dispatch, req:%#v\n", req)

	replyAddr, err := getPeerListenAddr(ctx, req.GetAddr())
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse %q into a peer control address", req.GetAddr())
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
func (svc *mgmtSvc) resolveRanks(hosts, ranks string) (hitRS, missRS *system.RankSet, missHS *hostlist.HostSet, err error) {
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
		//if hitRS, missHS, err = svc.membership.CheckHosts(hosts, svc.srvCfg.ControlPort); err != nil {
		if hitRS, missHS, err = svc.membership.CheckHosts(hosts, build.DefaultControlPort); err != nil {
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
func (svc *mgmtSvc) rpcFanout(parent context.Context, fanReq fanoutRequest, updateOnFail bool) (*fanoutResponse, *system.RankSet, error) {
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
// Retrieve the state of DAOS ranks in the system by returning details stored in
// the system membership. Request details for ranks provided in list (or all
// members if request rank list is empty).
//
// This control service method is triggered from the control API method of the
// same name in lib/control/system.go and returns results from all selected
// ranks.
func (svc *mgmtSvc) SystemQuery(ctx context.Context, req *mgmtpb.SystemQueryReq) (*mgmtpb.SystemQueryResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debug("Received SystemQuery RPC")

	hitRanks, missRanks, missHosts, err := svc.resolveRanks(req.Hosts, req.Ranks)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.SystemQueryResp{
		Absentranks: missRanks.String(),
		Absenthosts: missHosts.String(),
	}
	if hitRanks.Count() == 0 {
		return resp, nil
	}

	members := svc.membership.Members(hitRanks)
	if err := convert.Types(members, &resp.Members); err != nil {
		return nil, err
	}

	svc.log.Debugf("Responding to SystemQuery RPC: %+v", resp)

	return resp, nil
}

func populateStopResp(fanResp *fanoutResponse, pbResp *mgmtpb.SystemStopResp, action string) error {
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
func (svc *mgmtSvc) SystemStop(ctx context.Context, pbReq *mgmtpb.SystemStopReq) (*mgmtpb.SystemStopResp, error) {
	if err := svc.checkLeaderRequest(pbReq); err != nil {
		return nil, err
	}
	svc.log.Debug("Received SystemStop RPC")

	// TODO: consider locking to prevent join attempts when shutting down
	pbResp := new(mgmtpb.SystemStopResp)

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
func (svc *mgmtSvc) SystemStart(ctx context.Context, pbReq *mgmtpb.SystemStartReq) (*mgmtpb.SystemStartResp, error) {
	if err := svc.checkLeaderRequest(pbReq); err != nil {
		return nil, err
	}
	svc.log.Debug("Received SystemStart RPC")

	fanResp, _, err := svc.rpcFanout(ctx, fanoutRequest{
		Method: control.StartRanks,
		Hosts:  pbReq.GetHosts(),
		Ranks:  pbReq.GetRanks(),
	}, false)
	if err != nil {
		return nil, err
	}

	pbResp := &mgmtpb.SystemStartResp{
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
func (svc *mgmtSvc) SystemResetFormat(ctx context.Context, pbReq *mgmtpb.SystemResetFormatReq) (*mgmtpb.SystemResetFormatResp, error) {
	if err := svc.checkLeaderRequest(pbReq); err != nil {
		return nil, err
	}
	svc.log.Debug("Received SystemResetFormat RPC")

	// We can't rely on the db being up and running, as one of the
	// use cases for this command is to nuke the system from orbit
	// regardless of what state it's in. But we should at least enforce
	// that the RPC is being handled on a MS replica.
	if err := svc.sysdb.CheckReplica(); err != nil {
		return nil, err
	}

	fanResp, _, err := svc.rpcFanout(ctx, fanoutRequest{
		Method: control.ResetFormatRanks,
		Hosts:  pbReq.GetHosts(),
		Ranks:  pbReq.GetRanks(),
	}, false)
	if err != nil {
		return nil, err
	}

	pbResp := &mgmtpb.SystemResetFormatResp{
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

// ClusterEvent management service gRPC handler receives ClusterEvent requests
// from control-plane instances attempting to notify the MS of a cluster event
// in the DAOS system (this handler should only get called on the MS leader).
func (svc *mgmtSvc) ClusterEvent(ctx context.Context, req *sharedpb.ClusterEventReq) (*sharedpb.ClusterEventResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	resp, err := svc.events.HandleClusterEvent(req)
	if err != nil {
		return nil, errors.Wrapf(err, "handle cluster event %+v", req)
	}

	return resp, nil
}
