//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"strings"
	"time"

	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
	"google.golang.org/grpc/peer"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/logging"
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
	if svc.clientNetworkHint == nil {
		return nil, errors.New("clientNetworkHint is missing")
	}
	svc.log.Debugf("MgmtSvc.GetAttachInfo dispatch, req:%+v\n", *req)

	groupMap, err := svc.sysdb.GroupMap()
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.GetAttachInfoResp)
	if req.GetAllRanks() {
		for rank, entry := range groupMap.RankEntries {
			resp.RankUris = append(resp.RankUris, &mgmtpb.GetAttachInfoResp_RankUri{
				Rank: rank.Uint32(),
				Uri:  entry.URI,
			})
		}
	} else {
		// If the request does not indicate that all ranks should be returned,
		// it may be from an older client, in which case we should just return
		// the MS ranks.
		for _, rank := range groupMap.MSRanks {
			resp.RankUris = append(resp.RankUris, &mgmtpb.GetAttachInfoResp_RankUri{
				Rank: rank.Uint32(),
				Uri:  groupMap.RankEntries[rank].URI,
			})
		}
	}
	resp.ClientNetHint = svc.clientNetworkHint
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

// getPeerListenAddr provides the resolved TCP address where the peer server is listening.
func getPeerListenAddr(ctx context.Context, listenAddrStr string) (*net.TCPAddr, error) {
	ipAddr, portStr, err := net.SplitHostPort(listenAddrStr)
	if err != nil {
		return nil, errors.Wrap(err, "get listening port")
	}

	if ipAddr != "0.0.0.0" {
		// If the peer gave us an explicit IP address, just use it.
		return net.ResolveTCPAddr("tcp", listenAddrStr)
	}

	// If we got 0.0.0.0, we may be able to harvest the remote IP from the context.
	p, ok := peer.FromContext(ctx)
	if !ok {
		return nil, errors.New("peer details not found in context")
	}

	tcpAddr, ok := p.Addr.(*net.TCPAddr)
	if !ok {
		return nil, errors.Errorf("peer address (%s) not tcp", p.Addr)
	}

	// resolve combined IP/port address
	return net.ResolveTCPAddr(p.Addr.Network(),
		net.JoinHostPort(tcpAddr.IP.String(), portStr))
}

const (
	groupUpdateInterval = 500 * time.Millisecond
	batchJoinInterval   = 250 * time.Millisecond
)

type (
	batchJoinRequest struct {
		mgmtpb.JoinReq
		peerAddr *net.TCPAddr
		joinCtx  context.Context
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
	var groupUpdateNeeded bool

	joinTimer := time.NewTicker(batchJoinInterval)
	defer joinTimer.Stop()
	groupUpdateTimer := time.NewTicker(groupUpdateInterval)
	defer groupUpdateTimer.Stop()

	for {
		select {
		case <-parent.Done():
			svc.log.Debug("stopped joinLoop")
			return
		case sync := <-svc.groupUpdateReqs:
			groupUpdateNeeded = true
			if sync {
				if err := svc.doGroupUpdate(parent, true); err != nil {
					svc.log.Errorf("sync GroupUpdate failed: %s", err)
					continue
				}
				groupUpdateNeeded = false
			}
		case <-groupUpdateTimer.C:
			if !groupUpdateNeeded {
				continue
			}
			if err := svc.doGroupUpdate(parent, false); err != nil {
				svc.log.Errorf("async GroupUpdate failed: %s", err)
				continue
			}
			groupUpdateNeeded = false
		case jr := <-svc.joinReqs:
			joinReqs = append(joinReqs, jr)
		case <-joinTimer.C:
			if len(joinReqs) == 0 {
				continue
			}

			svc.log.Debugf("processing %d join requests", len(joinReqs))
			joinResps := make([]*batchJoinResponse, len(joinReqs))
			for i, req := range joinReqs {
				joinResps[i] = svc.join(parent, req)
			}

			// Reset groupUpdateNeeded here to avoid triggering it
			// again by timer. Any requests that were made between
			// the last timer and these join requests will be handled
			// here.
			groupUpdateNeeded = false
			if err := svc.doGroupUpdate(parent, false); err != nil {
				// If the call failed, however, make sure that
				// it gets called again by the timer. We have to
				// deal with the situation where a local MS service
				// rank is joining but isn't ready to handle dRPC
				// requests yet.
				groupUpdateNeeded = true
				if errors.Cause(err) != errInstanceNotReady {
					err = errors.Wrap(err, "failed to perform CaRT group update")
					for i, jr := range joinResps {
						if jr.joinErr == nil {
							joinResps[i] = &batchJoinResponse{joinErr: err}
						}
					}
				}
			}

			svc.log.Debugf("sending %d join responses", len(joinReqs))
			for i, req := range joinReqs {
				select {
				case <-parent.Done():
					svc.log.Errorf("joinLoop shut down before response sent: %s", parent.Err())
				case <-req.joinCtx.Done():
					svc.log.Errorf("failed to send join response: %s", req.joinCtx.Err())
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
		Incarnation:    req.GetIncarnation(),
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
			member.Rank, member.FabricURI, joinResponse.PrevState, member.State)
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

		if err := srv.SetupRank(ctx, joinResponse.Member.Rank); err != nil {
			return &batchJoinResponse{
				joinErr: errors.Wrap(err, "SetupRank on local instance failed"),
			}
		}
	}

	return resp
}

// reqGroupUpdate requests a group update.
func (svc *mgmtSvc) reqGroupUpdate(ctx context.Context, sync bool) {
	select {
	case <-ctx.Done():
	case svc.groupUpdateReqs <- sync:
	}
}

// doGroupUpdate performs a synchronous group update.
// NB: This method must not be called concurrently, as out-of-order
// group updates may trigger engine assertions.
func (svc *mgmtSvc) doGroupUpdate(ctx context.Context, forced bool) error {
	if forced {
		if err := svc.sysdb.IncMapVer(); err != nil {
			return err
		}
	}

	gm, err := svc.sysdb.GroupMap()
	if err != nil {
		return err
	}
	if len(gm.RankEntries) == 0 {
		return system.ErrEmptyGroupMap
	}
	if gm.Version == svc.lastMapVer {
		svc.log.Debugf("skipping duplicate GroupUpdate @ %d", gm.Version)
		return nil
	}
	if gm.Version < svc.lastMapVer {
		return errors.Errorf("group map version %d is less than last map version %d", gm.Version, svc.lastMapVer)
	}

	req := &mgmtpb.GroupUpdateReq{
		MapVersion: gm.Version,
	}
	rankSet := &system.RankSet{}
	for rank, entry := range gm.RankEntries {
		req.Engines = append(req.Engines, &mgmtpb.GroupUpdateReq_Engine{
			Rank:        rank.Uint32(),
			Uri:         entry.URI,
			Incarnation: entry.Incarnation,
		})
		rankSet.Add(rank)
	}

	// Final check to make sure we're still leader.
	if err := svc.sysdb.CheckLeader(); err != nil {
		return err
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
	svc.lastMapVer = gm.Version

	resp := new(mgmtpb.GroupUpdateResp)
	if err = proto.Unmarshal(dResp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal GroupUpdate response")
	}

	if resp.GetStatus() != 0 {
		return daos.Status(resp.GetStatus())
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
// primary group in the local engine.
//
// The state of the newly joined/excluded rank along with the reply address used
// to contact the new rank in future will be registered in the system membership.
// The reply address is generated by combining peer (sender) IP (from context)
// with listening port from joining instance's host addr contained in the
// provided request.
func (svc *mgmtSvc) Join(ctx context.Context, req *mgmtpb.JoinReq) (resp *mgmtpb.JoinResp, err error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.Join dispatch, req:%s", mgmtpb.Debug(req))
	defer func() {
		if err != nil {
			svc.log.Errorf("MgmtSvc.Join failed: %s", err)
			return
		}
		svc.log.Debugf("MgmtSvc.Join succeeded, resp:%s", mgmtpb.Debug(resp))
	}()

	replyAddr, err := getPeerListenAddr(ctx, req.GetAddr())
	if err != nil {
		return nil, errors.Wrapf(err, "failed to parse %q into a peer control address", req.GetAddr())
	}

	bjr := &batchJoinRequest{
		JoinReq:  *req,
		peerAddr: replyAddr,
		joinCtx:  ctx,
		respCh:   make(chan *batchJoinResponse),
	}

	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case svc.joinReqs <- bjr:
	}

	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case r := <-bjr.respCh:
		if r.joinErr != nil {
			return nil, r.joinErr
		}
		resp = &r.JoinResp
	}

	return resp, nil
}

type (
	// systemRanksFunc is an alias for control client API *Ranks() fanout
	// function that executes across ranks on different hosts.
	systemRanksFunc func(context.Context, control.UnaryInvoker, *control.RanksReq) (*control.RanksResp, error)

	fanoutRequest struct {
		Method     systemRanksFunc
		Ranks      *system.RankSet
		Force      bool
		FullSystem bool
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
		if hitRS, missHS, err = svc.membership.CheckHosts(hosts, build.DefaultControlPort); err != nil {
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

// synthesise "Stopped" rank results for any harness host errors
func addUnresponsiveResults(log logging.Logger, hostRanks map[string][]system.Rank, rr *control.RanksResp, resp *fanoutResponse) {
	for _, hes := range rr.HostErrors {
		for _, addr := range strings.Split(hes.HostSet.DerangedString(), ",") {
			for _, rank := range hostRanks[addr] {
				resp.Results = append(resp.Results,
					&system.MemberResult{
						Rank: rank, Msg: hes.HostError.Error(),
						State: system.MemberStateUnresponsive,
					})
			}
			log.Debugf("harness %s (ranks %v) host error: %s", addr, hostRanks[addr],
				hes.HostError)
		}
	}
}

// Remove any duplicate results from response.
func removeDuplicateResults(log logging.Logger, resp *fanoutResponse) {
	seenResults := make(map[uint32]*system.MemberResult)
	for _, res := range resp.Results {
		if res == nil {
			continue
		}
		rID := res.Rank.Uint32()
		if extant, existing := seenResults[rID]; !existing {
			seenResults[rID] = res
		} else if !extant.Equals(res) {
			log.Errorf("nonidentical result for same rank: %+v != %+v", *extant, *res)
		}
	}

	if len(seenResults) == len(resp.Results) {
		return
	}

	newResults := make(system.MemberResults, 0, len(seenResults))
	for _, res := range seenResults {
		newResults = append(newResults, res)
	}
	resp.Results = newResults
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
func (svc *mgmtSvc) rpcFanout(ctx context.Context, req *fanoutRequest, resp *fanoutResponse, updateOnFail bool) (*fanoutResponse, *system.RankSet, error) {
	if req == nil || req.Method == nil {
		return nil, nil, errors.New("nil fanout request or method")
	}
	if resp == nil {
		resp = new(fanoutResponse)
	}

	if req.Ranks.Count() == 0 {
		return resp, req.Ranks, nil
	}

	ranksReq := &control.RanksReq{
		Ranks: req.Ranks.String(), Force: req.Force,
	}

	funcName := func(i interface{}) string {
		return filepath.Base(runtime.FuncForPC(reflect.ValueOf(i).Pointer()).Name())
	}

	waiting := system.RankSetFromRanks(req.Ranks.Ranks())
	finished := system.MustCreateRankSet("")
	ranksReq.SetReportCb(func(hr *control.HostResponse) {
		rs, ok := hr.Message.(interface{ GetResults() []*sharedpb.RankResult })
		if !ok {
			svc.log.Errorf("unexpected message type in HostResponse: %T", hr.Message)
			return
		}

		for _, rr := range rs.GetResults() {
			waiting.Delete(system.Rank(rr.Rank))
			finished.Add(system.Rank(rr.Rank))
		}

		svc.log.Infof("%s: finished: %s; waiting: %s", funcName(req.Method), finished, waiting)
	})

	// Not strictly necessary but helps with debugging.
	dl, ok := ctx.Deadline()
	if ok {
		ranksReq.SetTimeout(time.Until(dl))
	}

	ranksReq.SetHostList(svc.membership.HostList(req.Ranks))
	ranksResp, err := req.Method(ctx, svc.rpcClient, ranksReq)
	if err != nil {
		return nil, nil, err
	}

	resp.Results = ranksResp.RankResults

	addUnresponsiveResults(svc.log, svc.membership.HostRanks(req.Ranks), ranksResp, resp)

	removeDuplicateResults(svc.log, resp)

	if len(resp.Results) != req.Ranks.Count() {
		svc.log.Debugf("expected %d results, got %d",
			req.Ranks.Count(), len(resp.Results))
	}

	if err = svc.membership.UpdateMemberStates(resp.Results, updateOnFail); err != nil {
		return nil, nil, err
	}

	return resp, req.Ranks, nil
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
	svc.log.Debugf("Received SystemQuery RPC: %+v", req)

	hitRanks, missRanks, missHosts, err := svc.resolveRanks(req.Hosts, req.Ranks)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.SystemQueryResp{
		Absentranks: missRanks.String(),
		Absenthosts: missHosts.String(),
	}
	if hitRanks.Count() == 0 {
		// If the membership is empty, this replica is likely waiting
		// for logs from peers, so we should indicate to the client
		// that it should try a different replica.
		if req.Ranks == "" && req.Hosts == "" {
			return nil, system.ErrRaftUnavail
		}
		return resp, nil
	}

	members := svc.membership.Members(hitRanks)
	if err := convert.Types(members, &resp.Members); err != nil {
		return nil, err
	}

	svc.log.Debugf("Responding to SystemQuery RPC: %s", mgmtpb.Debug(resp))

	return resp, nil
}

func fanout2pbStopResp(act string, fr *fanoutResponse) (*mgmtpb.SystemStopResp, error) {
	sr := &mgmtpb.SystemStopResp{}
	sr.Absentranks = fr.AbsentRanks.String()
	sr.Absenthosts = fr.AbsentHosts.String()

	if err := convert.Types(fr.Results, &sr.Results); err != nil {
		return nil, err
	}
	for _, r := range sr.Results {
		r.Action = act
	}

	return sr, nil
}

func newSystemStopFailedEvent(act, errs string) *events.RASEvent {
	return events.NewGenericEvent(events.RASSystemStopFailed, events.RASSeverityError,
		fmt.Sprintf("System shutdown failed during %q action, %s", act, errs), "")
}

// processStopResp will raise failed event if the response results contain
// errors, no event will be raised if user requested ranks or hosts that are
// absent in the membership. Fanout response will then be converted to protouf.
func processStopResp(act string, fr *fanoutResponse, publisher events.Publisher) (*mgmtpb.SystemStopResp, error) {
	if fr.Results.Errors() != nil {
		publisher.Publish(newSystemStopFailedEvent(act, fr.Results.Errors().Error()))
	}

	return fanout2pbStopResp(act, fr)
}

type systemReq interface {
	GetHosts() string
	GetRanks() string
}

func (svc *mgmtSvc) getFanout(req systemReq) (*fanoutRequest, *fanoutResponse, error) {
	if common.InterfaceIsNil(req) {
		return nil, nil, errors.New("nil system request")
	}

	// populate missing hosts/ranks in outer response and resolve active ranks
	hitRanks, missRanks, missHosts, err := svc.resolveRanks(req.GetHosts(), req.GetRanks())
	if err != nil {
		return nil, nil, err
	}
	allRanks, err := svc.membership.RankList()
	if err != nil {
		return nil, nil, err
	}

	force := false
	if forceReq, ok := req.(interface{ GetForce() bool }); ok {
		force = forceReq.GetForce()
	}
	return &fanoutRequest{
			Ranks:      hitRanks,
			Force:      force,
			FullSystem: len(system.CheckRankMembership(hitRanks.Ranks(), allRanks)) == 0,
		}, &fanoutResponse{
			AbsentRanks: missRanks,
			AbsentHosts: missHosts,
		}, nil
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
func (svc *mgmtSvc) SystemStop(ctx context.Context, req *mgmtpb.SystemStopReq) (*mgmtpb.SystemStopResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debug("Received SystemStop RPC")

	fReq, fResp, err := svc.getFanout(req)
	if err != nil {
		return nil, err
	}

	// First phase: Prepare the ranks for shutdown, but only if the request
	// is for an unforced full system stop.
	if fReq.FullSystem && !fReq.Force {
		fReq.Method = control.PrepShutdownRanks
		fResp, _, err = svc.rpcFanout(ctx, fReq, fResp, true)
		if err != nil {
			return nil, err
		}
		if fResp.Results.Errors() != nil {
			// return early if not forced and prep shutdown fails
			return processStopResp("prep shutdown", fResp, svc.events)
		}
	}

	// Second phase: Stop the ranks. If the request is forced, we will
	// kill the ranks immediately without a graceful shutdown.
	fReq.Method = control.StopRanks
	fResp, _, err = svc.rpcFanout(ctx, fReq, fResp, true)
	if err != nil {
		return nil, err
	}

	resp, err := processStopResp("stop", fResp, svc.events)
	if err != nil {
		return nil, err
	}
	svc.log.Debugf("Responding to SystemStop RPC: %+v", resp)

	return resp, nil
}

func newSystemStartFailedEvent(errs string) *events.RASEvent {
	return events.NewGenericEvent(events.RASSystemStartFailed, events.RASSeverityError,
		fmt.Sprintf("System startup failed, %s", errs), "")
}

// processStartResp will raise failed event if the response results contain
// errors, no event will be raised if user requested ranks or hosts that are
// absent in the membership. Fanout response will then be converted to protouf.
func processStartResp(fr *fanoutResponse, publisher events.Publisher) (*mgmtpb.SystemStartResp, error) {
	if fr.Results.Errors() != nil {
		publisher.Publish(newSystemStartFailedEvent(fr.Results.Errors().Error()))
	}

	sr := &mgmtpb.SystemStartResp{}
	sr.Absentranks = fr.AbsentRanks.String()
	sr.Absenthosts = fr.AbsentHosts.String()

	if err := convert.Types(fr.Results, &sr.Results); err != nil {
		return nil, err
	}
	for _, r := range sr.Results {
		r.Action = "start"
	}

	return sr, nil
}

// SystemStart implements the method defined for the Management Service.
//
// Initiate controlled start of DAOS system instances (system members)
// after a controlled shutdown using information in the membership registry.
// Return system start results.
//
// This control service method is triggered from the control API method of the
// same name in lib/control/system.go and returns results from all selected ranks.
func (svc *mgmtSvc) SystemStart(ctx context.Context, req *mgmtpb.SystemStartReq) (*mgmtpb.SystemStartResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debug("Received SystemStart RPC")

	fReq, fResp, err := svc.getFanout(req)
	if err != nil {
		return nil, err
	}

	fReq.Method = control.StartRanks
	fResp, _, err = svc.rpcFanout(ctx, fReq, fResp, true)
	if err != nil {
		return nil, err
	}

	resp, err := processStartResp(fResp, svc.events)
	if err != nil {
		return nil, err
	}
	svc.log.Debugf("Responding to SystemStart RPC: %+v", resp)

	return resp, nil
}

// ClusterEvent management service gRPC handler receives ClusterEvent requests
// from control-plane instances attempting to notify the MS of a cluster event
// in the DAOS system (this handler should only get called on the MS leader).
func (svc *mgmtSvc) ClusterEvent(ctx context.Context, req *sharedpb.ClusterEventReq) (*sharedpb.ClusterEventResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	// indicate to handler that event has been forwarded
	resp, err := svc.events.HandleClusterEvent(req, true)
	if err != nil {
		return nil, errors.Wrapf(err, "handle cluster event %+v", req)
	}

	return resp, nil
}

// eraseAndRestart is called on MS replicas to shut down the raft DB and
// remove its files before restarting the control plane server.
func (svc *mgmtSvc) eraseAndRestart(pause bool) error {
	svc.log.Infof("%s pid %d: erasing system db", build.ControlPlaneName, os.Getpid())

	if err := svc.sysdb.Stop(); err != nil {
		return errors.Wrap(err, "failed to stop system database")
	}
	if err := svc.sysdb.RemoveFiles(); err != nil {
		return errors.Wrap(err, "failed to remove system database")
	}

	myPath, err := os.Readlink("/proc/self/exe")
	if err != nil {
		return errors.Wrap(err, "unable to determine path to self")
	}

	go func() {
		if pause {
			time.Sleep(50 * time.Millisecond)
		}
		if err := unix.Exec(myPath, append([]string{myPath}, os.Args[1:]...), os.Environ()); err != nil {
			svc.log.Error(errors.Wrap(err, "Exec() failed").Error())
		}
	}()

	return nil
}

// SystemErase implements the gRPC handler for erasing system metadata.
func (svc *mgmtSvc) SystemErase(ctx context.Context, pbReq *mgmtpb.SystemEraseReq) (*mgmtpb.SystemEraseResp, error) {
	// At a minimum, ensure that this only runs on MS replicas.
	if err := svc.checkReplicaRequest(pbReq); err != nil {
		return nil, err
	}

	svc.log.Debug("Received SystemErase RPC")

	// If this is called on a non-leader replica, nuke the local
	// instance of the database and any superblocks, then restart.
	//
	// TODO (DAOS-7080): Rework this to remove redundancy and thoroughly
	// wipe SCM rather than removing things piecemeal.
	if !svc.sysdb.IsLeader() {
		for _, engine := range svc.harness.Instances() {
			if err := engine.Stop(unix.SIGKILL); err != nil {
				svc.log.Errorf("instance %d failed to stop: %s", engine.Index(), err)
			}
			if err := engine.RemoveSuperblock(); err != nil {
				svc.log.Errorf("instance %d failed to remove superblock: %s", engine.Index(), err)
			}
		}
		if err := svc.eraseAndRestart(false); err != nil {
			return nil, errors.Wrap(err, "erasing and restarting non-leader")
		}
	}

	// On the leader, we should first tell all servers to prepare for
	// reformat by wiping out their engine superblocks, etc.
	fanReq, fanResp, err := svc.getFanout(&mgmtpb.SystemQueryReq{})
	if err != nil {
		return nil, err
	}
	fanReq.Method = control.ResetFormatRanks
	fanResp, _, err = svc.rpcFanout(ctx, fanReq, fanResp, false)
	if err != nil {
		return nil, err
	}

	for _, mr := range fanResp.Results {
		svc.log.Debugf("member response: %#v", mr)
	}

	pbResp := new(mgmtpb.SystemEraseResp)
	if err := convert.Types(fanResp.Results, &pbResp.Results); err != nil {
		return nil, err
	}
	for _, result := range pbResp.Results {
		result.Action = "reset format"
	}

	if fanResp.Results.Errors() != nil {
		return pbResp, nil
	}

	// Next, tell all of the replicas to lobotomize themselves and restart.
	peers, err := svc.sysdb.PeerAddrs()
	if err != nil {
		return nil, err
	}
	for _, peer := range peers {
		peerReq := new(control.SystemEraseReq)
		peerReq.AddHost(peer.String())

		if _, err := control.SystemErase(ctx, svc.rpcClient, peerReq); err != nil {
			if control.IsRetryableConnErr(err) {
				continue
			}
			return nil, err
		}
	}

	// Finally, take care of the leader on the way out.
	return pbResp, errors.Wrap(svc.eraseAndRestart(true), "erasing and restarting leader")
}

// SystemCleanup implements the method defined for the Management Service.
//
// Signal to the data plane to find all resources associted with a given machine
// and release them. This includes releasing all container and pool handles associated
// with the machine.
//
func (svc *mgmtSvc) SystemCleanup(ctx context.Context, req *mgmtpb.SystemCleanupReq) (*mgmtpb.SystemCleanupResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("Received SystemCleanup RPC: %+v", req)

	if req.Machine == "" {
		return nil, errors.New("SystemCleanup requires a machine name.")
	}

	psList, err := svc.sysdb.PoolServiceList(false)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.SystemCleanupResp)
	evictReq := new(mgmtpb.PoolEvictReq)

	evictReq.Sys = req.Sys
	evictReq.Machine = req.Machine

	for _, ps := range psList {
		var errmsg string = ""

		// Use our incoming request and just replace the uuid on each iteration
		evictReq.Id = ps.PoolUUID.String()

		dresp, err := svc.makePoolServiceCall(ctx, drpc.MethodPoolEvict, evictReq)
		if err != nil {
			return nil, err
		}

		res := &mgmtpb.PoolEvictResp{}
		if err = proto.Unmarshal(dresp.Body, res); err != nil {
			res.Status = int32(daos.IOInvalid)
			errmsg = errors.Wrap(err, "unmarshal PoolEvict response").Error()
			res.Count = 0
		}

		if res.Status != int32(daos.Success) {
			errmsg = fmt.Sprintf("Unable to clean up handles for machine %s on pool %s", evictReq.Machine, evictReq.Id)
		}

		svc.log.Debugf("Response from pool evict in cleanup: %+v", res)
		resp.Results = append(resp.Results, &mgmtpb.SystemCleanupResp_CleanupResult{
			Status: res.Status,
			Msg:    errmsg,
			PoolId: evictReq.Id,
			Count:  uint32(res.Count),
		})
	}

	svc.log.Debugf("Responding to SystemCleanup RPC: %+v", resp)

	return resp, nil
}

// SystemSetAttr sets system-level attributes.
func (svc *mgmtSvc) SystemSetAttr(ctx context.Context, req *mgmtpb.SystemSetAttrReq) (_ *mgmtpb.DaosResp, err error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("Received SystemSetAttr RPC: %+v", req)
	defer func() {
		svc.log.Debugf("Responding to SystemSetAttr RPC: (%v)", err)
	}()

	if err := system.SetAttributes(svc.sysdb, req.GetAttributes()); err != nil {
		return nil, err
	}

	return &mgmtpb.DaosResp{}, nil
}

// SystemGetAttr gets system-level attributes.
func (svc *mgmtSvc) SystemGetAttr(ctx context.Context, req *mgmtpb.SystemGetAttrReq) (resp *mgmtpb.SystemGetAttrResp, err error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("Received SystemGetAttr RPC: %+v", req)
	defer func() {
		svc.log.Debugf("Responding to SystemGetAttr RPC: %+v (%v)", resp, err)
	}()

	props, err := system.GetAttributes(svc.sysdb, req.GetKeys())
	if err != nil {
		return nil, err
	}

	resp = &mgmtpb.SystemGetAttrResp{Attributes: props}
	return
}

// SystemSetProp sets user-visible system properties.
func (svc *mgmtSvc) SystemSetProp(ctx context.Context, req *mgmtpb.SystemSetPropReq) (_ *mgmtpb.DaosResp, err error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("Received SystemSetProp RPC: %+v", req)
	defer func() {
		svc.log.Debugf("Responding to SystemSetProp RPC: (%v)", err)
	}()

	if err := system.SetUserProperties(svc.sysdb, svc.systemProps, req.GetProperties()); err != nil {
		return nil, err
	}

	return &mgmtpb.DaosResp{}, nil
}

// SystemGetProp gets user-visible system properties.
func (svc *mgmtSvc) SystemGetProp(ctx context.Context, req *mgmtpb.SystemGetPropReq) (resp *mgmtpb.SystemGetPropResp, err error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("Received SystemGetProp RPC: %+v", req)
	defer func() {
		svc.log.Debugf("Responding to SystemGetProp RPC: %+v (%v)", resp, err)
	}()

	props, err := system.GetUserProperties(svc.sysdb, svc.systemProps, req.GetKeys())
	if err != nil {
		return nil, err
	}

	resp = &mgmtpb.SystemGetPropResp{Properties: props}
	return
}
