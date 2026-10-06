//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/dustin/go-humanize/english"
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/checker"
	"github.com/daos-stack/daos/src/control/system/raft"
)

const (
	fabricProviderProp   = "fabric_providers"
	groupUpdatePauseProp = "group_update_paused"
	domainLabelsProp     = "domain_labels"
	domainLabelsSep      = "=" // invalid in a label name
)

// exec is a function pointer wrapping unix.Exec() to enable mocking in unit tests.
var exec = unix.Exec

// GetAttachInfo handles a request to retrieve a map of ranks to fabric URIs, in addition
// to client network autoconfiguration hints.
//
// The default use case, where req.AllRanks is false, is for libdaos clients to obtain
// the client network autoconfiguration hints, and the set of ranks associated with MS
// replicas. If req.AllRanks is true, all ranks' fabric URIs are also given the client.
func (svc *mgmtSvc) GetAttachInfo(ctx context.Context, req *mgmtpb.GetAttachInfoReq) (*mgmtpb.GetAttachInfoResp, error) {
	if err := svc.checkReplicaRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}
	if len(svc.clientNetworkHint) == 0 {
		return nil, errors.New("clientNetworkHint is missing")
	}

	groupMap, err := svc.sysdb.GroupMap()
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.GetAttachInfoResp)
	rankURIs := groupMap.RankEntries
	if !req.GetAllRanks() {
		rankURIs = make(map[ranklist.Rank]raft.RankEntry)

		// If the request does not indicate that all ranks should be returned,
		// it may be from an older client, in which case we should just return
		// the MS ranks.
		for _, rank := range groupMap.MSRanks {
			rankURIs[rank] = groupMap.RankEntries[rank]
		}
	}

	for rank, entry := range rankURIs {
		if len(svc.clientNetworkHint) < len(entry.SecondaryURIs)+1 {
			return nil, errors.Errorf("not enough client network hints (%d) for rank %d URIs (%d)",
				len(svc.clientNetworkHint), rank, len(entry.SecondaryURIs)+1)
		}

		resp.RankUris = append(resp.RankUris, &mgmtpb.GetAttachInfoResp_RankUri{
			Rank:    rank.Uint32(),
			Uri:     entry.PrimaryURI,
			NumCtxs: entry.NumPrimaryCtxs,
		})

		for i, uri := range entry.SecondaryURIs {
			rankURI := &mgmtpb.GetAttachInfoResp_RankUri{
				Rank:        rank.Uint32(),
				Uri:         uri,
				ProviderIdx: uint32(i + 1),
				NumCtxs:     entry.NumSecondaryCtxs[i],
			}

			resp.SecondaryRankUris = append(resp.SecondaryRankUris, rankURI)
		}
	}

	resp.ClientNetHint = svc.clientNetworkHint[0]
	if len(svc.clientNetworkHint) > 1 {
		resp.SecondaryClientNetHints = svc.clientNetworkHint[1:]
	}

	resp.MsRanks = ranklist.RanksToUint32(groupMap.MSRanks)

	v, err := svc.sysdb.DataVersion()
	if err != nil {
		return nil, err
	}
	resp.DataVersion = v

	resp.Sys = svc.sysdb.SystemName()

	if dv, err := build.NewVersion(build.DaosVersion); err == nil {
		resp.BuildInfo = &mgmtpb.BuildInfo{
			Major: uint32(dv.Major),
			Minor: uint32(dv.Minor),
			Patch: uint32(dv.Patch),
			Tag:   build.BuildInfo,
		}
	}

	return resp, nil
}

// LeaderQuery returns the system leader and MS replica details.
func (svc *mgmtSvc) LeaderQuery(ctx context.Context, req *mgmtpb.LeaderQueryReq) (*mgmtpb.LeaderQueryResp, error) {
	if err := svc.checkSystemRequest(req); err != nil {
		return nil, err
	}

	leaderAddr, replicas, err := svc.sysdb.LeaderQuery()
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.LeaderQueryResp{
		CurrentLeader: leaderAddr,
		Replicas:      replicas,
	}

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

// Check rank to be replaced is excluded from all it's pools.
// 1. Get potential replacement rank from membership
// 2. Retrieve pool-rank map for pools to query
// 3. Query each pool that rank belongs to
// 4. Check rank is not in response list of enabled ranks
func (svc *mgmtSvc) checkReplaceRank(ctx context.Context, rankToReplace ranklist.Rank) error {
	if rankToReplace == ranklist.NilRank {
		return errors.New("checking replace mode rank, nil rank supplied")
	}

	// Retrieve rank-to-pool mappings.
	rl := ranklist.RankList{rankToReplace}
	poolIDs, _, err := svc.getPoolRanksEnabled(ctx, ranklist.RankSetFromRanks(rl))
	if err != nil {
		return err
	}

	if len(poolIDs) != 0 {
		return FaultJoinReplaceEnabledPoolRank(rankToReplace, poolIDs...)
	}

	return nil
}

// join handles a request to join the system and is called from
// the batch processing goroutine.
func (svc *mgmtSvc) join(ctx context.Context, req *mgmtpb.JoinReq, peerAddr *net.TCPAddr) (*mgmtpb.JoinResp, error) {
	uuid, err := uuid.Parse(req.Uuid)
	if err != nil {
		return nil, errors.Wrapf(err, "invalid uuid %q", req.Uuid)
	}

	fd, err := svc.verifyFaultDomain(req)
	if err != nil {
		return nil, err
	}

	if err := svc.checkReqFabricProvider(req, peerAddr, svc.events); err != nil {
		return nil, err
	}

	joinReq := &system.JoinRequest{
		Rank:                    ranklist.Rank(req.Rank),
		UUID:                    uuid,
		ControlAddr:             peerAddr,
		PrimaryFabricURI:        req.Uri,
		SecondaryFabricURIs:     req.SecondaryUris,
		FabricContexts:          req.Nctxs,
		SecondaryFabricContexts: req.SecondaryNctxs,
		FaultDomain:             fd,
		Incarnation:             req.Incarnation,
		CheckMode:               req.CheckMode,
		Replace:                 req.Replace,
	}

	if req.Replace {
		rankToReplace, err := svc.membership.FindRankFromJoinRequest(joinReq)
		if err != nil {
			return nil, err
		}
		if err := svc.checkReplaceRank(ctx, rankToReplace); err != nil {
			return nil, errors.Wrapf(err, "join: replace rank %d", rankToReplace)
		}
		joinReq.Rank = rankToReplace
	}

	joinResponse, err := svc.membership.Join(joinReq)
	if err != nil {
		if system.IsJoinFailure(err) {
			publishJoinFailedEvent(req, peerAddr, svc.events, err.Error())
		}
		return nil, errors.Wrap(err, "failed to join system")
	}

	member := joinResponse.Member
	if joinResponse.Created {
		svc.log.Debugf("new system member: rank %d, addr %s, primary uri %s, secondary uris %s",
			member.Rank, peerAddr, member.PrimaryFabricURI, member.SecondaryFabricURIs)
	} else {
		svc.log.Debugf("updated system member: rank %d, primary uri %s, secondary uris %s, %s->%s",
			member.Rank, member.PrimaryFabricURI, member.SecondaryFabricURIs, joinResponse.PrevState, member.State)
	}

	joinState := mgmtpb.JoinResp_IN
	if svc.checkerIsEnabled() {
		joinState = mgmtpb.JoinResp_CHECK
	}
	resp := &mgmtpb.JoinResp{
		State:      joinState,
		Rank:       member.Rank.Uint32(),
		MapVersion: joinResponse.MapVersion,
	}

	if svc.isGroupUpdatePaused() && svc.allRanksJoined() {
		if err := svc.resumeGroupUpdate(); err != nil {
			svc.log.Errorf("failed to resume group update: %s", err.Error())
		}
		// join loop will trigger a new group update after this
	}

	// If the rank is local to the MS leader, then we need to wire up at least
	// one in order to perform a CaRT group update.
	if common.IsLocalAddr(peerAddr) && req.Idx == 0 {
		resp.LocalJoin = true

		srvs := svc.harness.Instances()
		if len(srvs) == 0 {
			return nil, errors.New("invalid Join request (index 0 doesn't exist?!?)")
		}
		srv := srvs[0]

		if err := srv.SetupRank(ctx, joinResponse.Member.Rank, joinResponse.MapVersion); err != nil {
			return nil, errors.Wrap(err, "SetupRank on local instance failed")
		}
	}

	return resp, nil
}

func (svc *mgmtSvc) verifyFaultDomain(req *mgmtpb.JoinReq) (*system.FaultDomain, error) {
	fd, err := system.NewFaultDomainFromString(req.SrvFaultDomain)
	if err != nil {
		return nil, config.FaultConfigFaultDomainInvalid(err)
	}

	if fd.Empty() {
		return nil, errors.New("no fault domain in join request")
	}

	labels := fd.Labels
	if !fd.HasLabels() {
		// While saving the labels, an unlabeled fault domain sets the labels to empty
		// strings. This allows us to distinguish between unset and unlabeled.
		labels = make([]string, fd.NumLevels())
	}

	sysLabels, err := svc.getDomainLabels()
	if system.IsErrSystemAttrNotFound(err) {
		svc.log.Debugf("setting fault domain labels for the first time: %+v", labels)
		if err := svc.setDomainLabels(labels); err != nil {
			return nil, errors.Wrap(err, "failed to set fault domain labels")
		}
		return fd, nil
	}
	if err != nil {
		return nil, errors.Wrap(err, "failed to get current fault domain labels")
	}

	// If system labels are all empty strings, that indicates an unlabeled system. In errors
	// and logging, clearer to present this as a completely empty array.
	var printSysLabels []string
	if sysLabels[0] != "" {
		printSysLabels = sysLabels
	}

	svc.log.Tracef("system labels: [%s], request labels: [%s]", strings.Join(printSysLabels, ", "), strings.Join(labels, ", "))
	if len(sysLabels) != len(labels) {
		return nil, FaultBadFaultDomainLabels(req.SrvFaultDomain, req.Uri, fd.Labels, printSysLabels)
	}
	for i := range sysLabels {
		if labels[i] != sysLabels[i] {
			return nil, FaultBadFaultDomainLabels(req.SrvFaultDomain, req.Uri, fd.Labels, printSysLabels)
		}
	}
	return fd, nil
}

func (svc *mgmtSvc) getDomainLabels() ([]string, error) {
	propStr, err := system.GetMgmtProperty(svc.sysdb, domainLabelsProp)
	if err != nil {
		return nil, err
	}
	return strings.Split(propStr, domainLabelsSep), nil
}

func (svc *mgmtSvc) setDomainLabels(labels []string) error {
	propStr := strings.Join(labels, domainLabelsSep)
	return system.SetMgmtProperty(svc.sysdb, domainLabelsProp, propStr)
}

// allRanksJoined checks whether all ranks that the system knows about, and that are not admin
// excluded, are joined.
//
// NB: This checks the state to determine if the rank is joined. There is a potential hole here,
// in a case where the system was killed with ranks in the joined state, rather than stopping the
// ranks first. In that case we may fire this off too early.
func (svc *mgmtSvc) allRanksJoined() bool {
	var total int
	var joined int
	var err error
	if total, err = svc.sysdb.MemberCount(); err != nil {
		svc.log.Errorf("failed to get total member count: %s", err)
		return false
	}

	if joined, err = svc.sysdb.MemberCount(system.MemberStateJoined, system.MemberStateAdminExcluded); err != nil {
		svc.log.Errorf("failed to get joined member count: %s", err)
		return false
	}

	return total == joined
}

func (svc *mgmtSvc) checkReqFabricProvider(req *mgmtpb.JoinReq, peerAddr *net.TCPAddr, publisher events.Publisher) error {
	joinProv, err := getProviderFromURI(req.Uri)
	if err != nil {
		return err
	}

	sysProv, err := svc.getFabricProvider()
	if err != nil {
		if system.IsErrSystemAttrNotFound(err) {
			svc.log.Debugf("error fetching system fabric provider: %s", err.Error())
			return system.ErrLeaderStepUpInProgress
		}
		return errors.Wrap(err, "fetching system fabric provider")
	}

	if joinProv != sysProv {
		msg := fmt.Sprintf("rank %d fabric provider %q does not match system provider %q",
			req.Rank, joinProv, sysProv)

		publishJoinFailedEvent(req, peerAddr, publisher, msg)
		return errors.New(msg)
	}

	return nil
}

func publishJoinFailedEvent(req *mgmtpb.JoinReq, peerAddr *net.TCPAddr, publisher events.Publisher, msg string) {
	publisher.Publish(events.NewEngineJoinFailedEvent(peerAddr.String(), req.Idx, req.Rank, req.Incarnation, msg))
}

func getProviderFromURI(uri string) (string, error) {
	uriParts := strings.Split(uri, "://")
	if len(uriParts) < 2 {
		return "", fmt.Errorf("unable to parse fabric provider from URI %q", uri)
	}
	return uriParts[0], nil
}

func (svc *mgmtSvc) getFabricProvider() (string, error) {
	return system.GetMgmtProperty(svc.sysdb, fabricProviderProp)
}

func (svc *mgmtSvc) setFabricProviders(val string) error {
	return system.SetMgmtProperty(svc.sysdb, fabricProviderProp, val)
}

func (svc *mgmtSvc) isGroupUpdatePaused() bool {
	propStr, err := system.GetMgmtProperty(svc.sysdb, groupUpdatePauseProp)
	if err != nil {
		return false
	}
	result, err := strconv.ParseBool(propStr)
	if err != nil {
		svc.log.Errorf("invalid value for mgmt prop %q: %s", groupUpdatePauseProp, err.Error())
		return false
	}
	return result
}

func (svc *mgmtSvc) pauseGroupUpdate() error {
	return system.SetMgmtProperty(svc.sysdb, groupUpdatePauseProp, "true")
}

func (svc *mgmtSvc) resumeGroupUpdate() error {
	return system.SetMgmtProperty(svc.sysdb, groupUpdatePauseProp, "false")
}

func (svc *mgmtSvc) updateFabricProviders(provList []string, publisher events.Publisher) error {
	provStr := strings.Join(provList, ",")

	curProv, err := svc.getFabricProvider()
	if system.IsErrSystemAttrNotFound(err) {
		svc.log.Debugf("setting system fabric providers (%s) for the first time", provStr)

		if err := svc.setFabricProviders(provStr); err != nil {
			return errors.Wrapf(err, "setting fabric provider for the first time")
		}
		return nil
	}
	if err != nil {
		return errors.Wrapf(err, "fetching current mgmt property %q", fabricProviderProp)
	}

	if provStr != curProv {
		numJoined, err := svc.sysdb.MemberCount(system.MemberStateJoined)
		if err != nil {
			return errors.Wrapf(err, "getting number of joined members")
		}
		if numJoined > 0 {
			return errors.Errorf("cannot change system provider %q to %q: %d member(s) already joined",
				curProv, provStr, numJoined)
		}

		if err := svc.pauseGroupUpdate(); err != nil {
			return errors.Wrapf(err, "unable to pause group update before provider change")
		}

		if err := svc.setFabricProviders(provStr); err != nil {
			if guErr := svc.resumeGroupUpdate(); guErr != nil {
				// something is very wrong if this happens
				svc.log.Errorf("unable to resume group update after provider change failed: %s", guErr.Error())
			}

			return errors.Wrapf(err, "changing fabric provider prop")
		}
		publisher.Publish(newFabricProvChangedEvent(curProv, provStr))
		return nil
	}

	svc.log.Tracef("system fabric provider value has not changed (%s)", provStr)
	return nil
}

func newFabricProvChangedEvent(o, n string) *events.RASEvent {
	return events.NewGenericEvent(events.RASSystemFabricProvChanged, events.RASSeverityNotice,
		fmt.Sprintf("system fabric provider has changed: %s -> %s", o, n), "")
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
	if svc.isGroupUpdatePaused() {
		svc.log.Debugf("group update requested (force: %v), but temporarily paused", forced)
		return nil
	}

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
	rankSet := &ranklist.RankSet{}
	for rank, entry := range gm.RankEntries {
		req.Engines = append(req.Engines, &mgmtpb.GroupUpdateReq_Engine{
			Rank:        rank.Uint32(),
			Uri:         entry.PrimaryURI,
			Incarnation: entry.Incarnation,
		})
		rankSet.Add(rank)
	}

	// Final check to make sure we're still leader.
	if err := svc.sysdb.CheckLeader(); err != nil {
		return err
	}

	svc.log.Debugf("group update request: version: %d, ranks: %s", req.MapVersion, rankSet)
	dResp, err := svc.harness.CallDrpc(ctx, daos.MethodGroupUpdate, req)
	if err != nil {
		if err == errEngineNotReady {
			return err
		}
		svc.log.Errorf("dRPC GroupUpdate call failed: %s", err)
		return err
	}
	svc.lastMapVer = gm.Version

	resp := new(mgmtpb.GroupUpdateResp)
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return err
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
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	msg, err := svc.submitBatchRequest(ctx, req)
	if err != nil {
		return nil, err
	}
	return msg.(*mgmtpb.JoinResp), nil
}

type (
	// systemRanksFunc is an alias for control client API *Ranks() fanout
	// function that executes across ranks on different hosts.
	systemRanksFunc func(context.Context, control.UnaryInvoker, *control.RanksReq) (*control.RanksResp, error)

	fanoutRequest struct {
		Method     systemRanksFunc
		Ranks      *ranklist.RankSet
		Force      bool
		FullSystem bool
		CheckMode  bool
	}

	fanoutResponse struct {
		Results     system.MemberResults
		AbsentHosts *hostlist.HostSet
		AbsentRanks *ranklist.RankSet
	}
)

// resolveRanks derives ranks to be used for fanout by comparing host and rank
// sets with the contents of the membership.
func (svc *mgmtSvc) resolveRanks(hosts, ranks string) (hitRS, missRS *ranklist.RankSet, missHS *hostlist.HostSet, err error) {
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
		// Empty rank/host sets implies include all ranks so pass empty
		// string to CheckRanks() to retrieve full rankset.
		if hitRS, missRS, err = svc.membership.CheckRanks(""); err != nil {
			return
		}
	}

	if missHS == nil {
		missHS = new(hostlist.HostSet)
	}
	if missRS == nil {
		missRS = new(ranklist.RankSet)
	}

	return
}

// synthesize "Stopped" rank results for any harness host errors
func addUnresponsiveResults(log logging.Logger, hostRanks map[string][]ranklist.Rank, rr *control.RanksResp, resp *fanoutResponse) {
	for _, hes := range rr.HostErrors {
		for _, addr := range strings.Split(hes.HostSet.DerangedString(), ",") {
			for _, rank := range hostRanks[addr] {
				resp.Results = append(resp.Results, system.NewMemberResult(rank,
					hes.HostError, system.MemberStateUnresponsive))
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
func (svc *mgmtSvc) rpcFanout(ctx context.Context, req *fanoutRequest, resp *fanoutResponse, updateOnFail bool) (*fanoutResponse, *ranklist.RankSet, error) {
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
		Ranks:     req.Ranks.String(),
		Force:     req.Force,
		CheckMode: req.CheckMode,
	}

	funcName := func(i interface{}) string {
		return filepath.Base(runtime.FuncForPC(reflect.ValueOf(i).Pointer()).Name())
	}

	waiting := ranklist.RankSetFromRanks(req.Ranks.Ranks())
	finished := ranklist.MustCreateRankSet("")
	ranksReq.SetReportCb(func(hr *control.HostResponse) {
		rs, ok := hr.Message.(interface{ GetResults() []*sharedpb.RankResult })
		if !ok {
			svc.log.Errorf("unexpected message type in HostResponse: %T", hr.Message)
			return
		}

		for _, rr := range rs.GetResults() {
			waiting.Delete(ranklist.Rank(rr.Rank))
			finished.Add(ranklist.Rank(rr.Rank))
		}

		msg := fmt.Sprintf("%s: ", funcName(req.Method))
		if finished.Count() != 0 {
			msg = fmt.Sprintf(" finished: %q", finished)
		}
		if waiting.Count() != 0 {
			msg = fmt.Sprintf(" waiting: %q", waiting)
		}
		svc.log.Infof(msg)
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

	hostRanks := svc.membership.HostRanks(req.Ranks)
	addUnresponsiveResults(svc.log, hostRanks, ranksResp, resp)

	removeDuplicateResults(svc.log, resp)

	if len(resp.Results) != req.Ranks.Count() {
		svc.log.Debugf("expected %d results, got %d",
			req.Ranks.Count(), len(resp.Results))
	}

	// Normally UpdateMemberStates() (below) opportunistically fills in each result's
	// Addr field from the membership DB as a side effect of persisting state via raft.
	// When raft is unavailable (see below) that never runs, so do it here unconditionally
	// from the same in-memory host/rank mapping used for addUnresponsiveResults() above --
	// this is a plain read of already-fetched data, not a raft write, so it works
	// regardless of leadership state. Without this, results returned to the client (e.g.
	// during SystemErase's post-raft-stop ResetFormatRanks fanout) have an empty Addr,
	// which callers such as dmg's system-erase result rendering treat as a hard error.
	rankAddrs := make(map[ranklist.Rank]string)
	for addr, ranks := range hostRanks {
		for _, rank := range ranks {
			rankAddrs[rank] = addr
		}
	}
	for _, result := range resp.Results {
		if result.Addr == "" {
			result.Addr = rankAddrs[result.Rank]
		}
	}

	// Only persist member state updates via raft if this node is still the raft leader.
	// FIXME: Losing leadership mid-fanout is rare and and if actions have already been
	// performed then we should find some way of persisting state change with the new leader.
	// In that case, preserving the RPC results is preferable to discarding them because the
	// state can no longer be durably recorded.
	if svc.sysdb.IsLeader() {
		if err = svc.membership.UpdateMemberStates(resp.Results, updateOnFail); err != nil {
			return nil, nil, err
		}
	} else {
		svc.log.Debugf("rpcFanout: not raft leader, skipping membership state update for %s", funcName(req.Method))
	}

	return resp, req.Ranks, nil
}

func (svc *mgmtSvc) getSysSelfHeal() (string, error) {
	if sh, err := system.GetUserProperty(svc.sysdb, svc.systemProps,
		daos.SystemPropertySelfHeal.String()); err != nil {
		return "", errors.Wrapf(err, "retrieving %s system property",
			daos.SystemPropertySelfHeal)
	} else {
		return sh, nil
	}
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
	if err := svc.checkReplicaRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	hitRanks, missRanks, missHosts, err := svc.resolveRanks(req.Hosts, req.Ranks)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.SystemQueryResp{
		Absentranks: missRanks.String(),
		Absenthosts: missHosts.String(),
	}

	// Retrieve system self-heal property. Assume default value where all flags are set if
	// property isn't present.
	resp.SysSelfHealPolicy = daos.DefaultSysSelfHealFlagsStr
	if selfHeal, err := svc.getSysSelfHeal(); system.IsErrSystemAttrNotFound(err) {
		svc.log.Debugf(err.Error())
	} else if err != nil {
		return nil, err
	} else {
		resp.SysSelfHealPolicy = selfHeal
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

	if req.StateMask == 0 {
		req.StateMask = uint32(system.AllMemberFilter)
	}

	members, err := svc.membership.Members(hitRanks, system.MemberState(req.StateMask))
	if err != nil {
		return nil, errors.Wrap(err, "get membership")
	}

	if err := convert.Types(members, &resp.Members); err != nil {
		return nil, err
	}

	for _, hint := range svc.clientNetworkHint {
		resp.Providers = append(resp.Providers, hint.Provider)
	}

	v, err := svc.sysdb.DataVersion()
	if err != nil {
		return nil, err
	}
	resp.DataVersion = v

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
			FullSystem: len(ranklist.CheckRankMembership(hitRanks.Ranks(), allRanks)) == 0,
		}, &fanoutResponse{
			AbsentRanks: missRanks,
			AbsentHosts: missHosts,
		}, nil
}

func (svc *mgmtSvc) getFanoutNoAdminExcluded(req systemReq, ignoreAdminExcluded bool) (*fanoutRequest, *fanoutResponse, error) {
	fReq, fResp, err := svc.getFanout(req)
	if err != nil {
		return nil, nil, err
	}

	// If ranks not explicitly requested, or the caller wants to ignore admin-excluded ranks in the range, we can filter them out.
	if ignoreAdminExcluded || (req.GetRanks() == "" && req.GetHosts() == "") {
		svc.filterAdminExcludedRanks(fReq)
		if fReq.Ranks.Count() == 0 {
			return nil, nil, errors.New("all requested ranks are administratively excluded")
		}
	} else if err := svc.checkRanksAdminExcluded(fReq.Ranks.Ranks()); err != nil {
		// The user explicitly requested admin-excluded ranks
		return nil, nil, err
	}
	return fReq, fResp, nil
}

func (svc *mgmtSvc) filterAdminExcludedRanks(fReq *fanoutRequest) {
	for _, r := range fReq.Ranks.Ranks() {
		if svc.membership.IsRankAdminExcluded(r) {
			svc.log.Tracef("filtering admin-excluded rank %d from request", r)
			fReq.Ranks.Delete(r)
		}
	}
}

func (svc *mgmtSvc) checkRanksAdminExcluded(ranks ranklist.RankList) error {
	var adminExcludedRanks ranklist.RankList
	for _, r := range ranks {
		if svc.membership.IsRankAdminExcluded(r) {
			adminExcludedRanks = append(adminExcludedRanks, r)
		}
	}
	if len(adminExcludedRanks) > 0 {
		return FaultRankAdminExcluded(adminExcludedRanks)
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
func (svc *mgmtSvc) SystemStop(ctx context.Context, req *mgmtpb.SystemStopReq) (*mgmtpb.SystemStopResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}
	svc.log.Debug("Received SystemStop RPC")

	fReq, fResp, err := svc.getFanoutNoAdminExcluded(req, req.IgnoreAdminExcluded)
	if err != nil {
		return nil, err
	}

	// First phase: Prepare the ranks for shutdown, but only if the request is for an unforced
	// full system stop.
	if !fReq.Force {
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

func (svc *mgmtSvc) checkMemberStates(requiredStates ...system.MemberState) error {
	var stateMask system.MemberState
	for _, state := range requiredStates {
		stateMask |= state
	}

	allMembers, err := svc.sysdb.AllMembers()
	if err != nil {
		return err
	}
	invalidMembers := &ranklist.RankSet{}

	svc.log.Tracef("checking %d members", len(allMembers))
	for _, m := range allMembers {
		svc.log.Tracef("member %d: %s", m.Rank.Uint32(), m.State)
		if m.State&stateMask == 0 {
			invalidMembers.Add(m.Rank)
		}
	}

	stopRequired := false
	if stateMask&system.MemberStateStopped != 0 {
		stopRequired = true
	}
	if invalidMembers.Count() > 0 {
		states := make([]string, len(requiredStates))
		for i, state := range requiredStates {
			states[i] = state.String()
		}
		return checker.FaultIncorrectMemberStates(stopRequired, invalidMembers.String(), strings.Join(states, "|"))
	}

	return nil
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
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}
	svc.log.Debug("Received SystemStart RPC")

	fReq, fResp, err := svc.getFanoutNoAdminExcluded(req, req.IgnoreAdminExcluded)
	if err != nil {
		return nil, err
	}

	fReq.CheckMode = req.CheckMode
	fReq.Method = control.StartRanks
	fResp, _, err = svc.rpcFanout(ctx, fReq, fResp, true)
	if err != nil {
		return nil, err
	}

	resp, err := processStartResp(fResp, svc.events)
	if err != nil {
		return nil, err
	}

	return resp, nil
}

// SystemExclude marks the specified ranks as administratively excluded from the system.
func (svc *mgmtSvc) SystemExclude(ctx context.Context, req *mgmtpb.SystemExcludeReq) (*mgmtpb.SystemExcludeResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	if req.Hosts == "" && req.Ranks == "" {
		return nil, errors.New("no hosts or ranks specified")
	}

	fReq, fResp, err := svc.getFanout(req)
	if err != nil {
		return nil, err
	}

	if fResp.AbsentHosts.Count() > 0 {
		return nil, errors.Errorf("invalid host(s): %s", fResp.AbsentHosts.String())
	}
	if fResp.AbsentRanks.Count() > 0 {
		return nil, errors.Errorf("invalid rank(s): %s", fResp.AbsentRanks.String())
	}

	resp := new(mgmtpb.SystemExcludeResp)
	for _, r := range fReq.Ranks.Ranks() {
		m, err := svc.sysdb.FindMemberByRank(r)
		if err != nil {
			return nil, err
		}
		action := "set admin-excluded state"
		m.State = system.MemberStateAdminExcluded
		if req.Clear {
			action = "clear admin-excluded state"
			m.State = system.MemberStateExcluded // cleared on rejoin
		}
		if err := svc.sysdb.UpdateMember(m); err != nil {
			return nil, err
		}
		resp.Results = append(resp.Results, &sharedpb.RankResult{
			Rank:   r.Uint32(),
			Action: action,
			State:  strings.ToLower(m.State.String()),
			Addr:   m.Addr.String(),
		})
	}

	svc.reqGroupUpdate(ctx, false)

	return resp, nil
}

func (svc *mgmtSvc) refuseUnavailableRanks(hosts, ranks string) (*ranklist.RankSet, error) {
	if hosts == "" && ranks == "" {
		return nil, errors.New("no hosts or ranks specified")
	}

	hitRanks, missRanks, missHosts, err := svc.resolveRanks(hosts, ranks)
	if err != nil {
		return nil, err
	}

	if missHosts.Count() > 0 {
		return nil, errors.Errorf("invalid host(s): %s", missHosts.String())
	}
	if missRanks.Count() > 0 {
		return nil, errors.Errorf("invalid rank(s): %s", missRanks.String())
	}
	if hitRanks.Count() == 0 {
		return nil, errors.New("no ranks to operate on")
	}

	// Refuse to operate on AdminExcluded rank.
	if err := svc.checkRanksAdminExcluded(hitRanks.Ranks()); err != nil {
		return nil, err
	}

	return hitRanks, nil
}

func (svc *mgmtSvc) queryPool(ctx context.Context, id string, getEnabled bool) (*ranklist.RankSet, error) {
	qmBits := daos.PoolQueryOptionDisabledEngines
	if getEnabled {
		qmBits = daos.PoolQueryOptionEnabledEngines
	}

	req := &mgmtpb.PoolQueryReq{
		Id:        id,
		Sys:       svc.sysdb.SystemName(),
		QueryMask: uint64(daos.MustNewPoolQueryMask(qmBits)),
	}

	resp, err := svc.PoolQuery(ctx, req)
	if err != nil {
		return nil, errors.Wrap(err, "query on pool failed")
	}

	rankStr := resp.DisabledRanks
	if getEnabled {
		rankStr = resp.EnabledRanks
	}
	svc.log.Tracef("query on pool %s (getEnabled=%v) returned rankset %q", id, getEnabled,
		rankStr)

	return ranklist.MustCreateRankSet(rankStr), nil
}

func (svc *mgmtSvc) getPoolIDs() ([]string, error) {
	psList, err := svc.sysdb.PoolServiceList(false)
	if err != nil {
		return nil, err
	}

	var poolIDs []string
	for _, ps := range psList {
		// Label preferred over UUID.
		poolID := ps.PoolLabel
		if poolID == "" {
			poolID = ps.PoolUUID.String()
		}
		poolIDs = append(poolIDs, poolID)
	}
	sort.Strings(poolIDs)

	return poolIDs, err
}

type poolRanksMap map[string]*ranklist.RankSet

// Build mappings of pools to any ranks that match the input filter by iterating through the pool
// service list. Identify pools by label if possible.
func (svc *mgmtSvc) getPoolRanks(ctx context.Context, filterRanks *ranklist.RankSet, getEnabled bool) ([]string, poolRanksMap, error) {
	filterRanksMap := make(map[ranklist.Rank]struct{})
	for _, r := range filterRanks.Ranks() {
		filterRanksMap[r] = struct{}{}
	}

	poolIDs, err := svc.getPoolIDs()
	if err != nil {
		return nil, nil, err
	}

	var outPoolIDs []string
	poolRanks := make(poolRanksMap)

	for _, poolID := range poolIDs {
		// Pool service entries in MS-db aren't synced with pool-rank mappings so build map
		// from PoolQuery calls. Return either enabled or disabled ranks in map based on the
		// getEnabled flag value passed.
		ranks, err := svc.queryPool(ctx, poolID, getEnabled)
		if err != nil {
			return nil, nil, err
		}

		svc.log.Tracef("pool-service detected: id %s, ranks %v", poolID, ranks)

		for _, r := range ranks.Ranks() {
			// Empty input rankset implies match all.
			if _, exists := filterRanksMap[r]; !exists && len(filterRanksMap) > 0 {
				continue
			}
			if _, exists := poolRanks[poolID]; !exists {
				poolRanks[poolID] = ranklist.MustCreateRankSet("")
				outPoolIDs = append(outPoolIDs, poolID)
			}
			poolRanks[poolID].Add(r)
		}
	}
	svc.log.Debugf("pool-ranks to operate on: %v", poolRanks)

	// Sanity check.
	if len(outPoolIDs) != len(poolRanks) {
		return nil, nil, errors.Errorf("nr poolIDs (%d) should be equal to nr poolRanks "+
			"keys (%d)", len(outPoolIDs), len(poolRanks))
	}

	return outPoolIDs, poolRanks, nil
}

func (svc *mgmtSvc) getPoolRanksEnabled(ctx context.Context, ranks *ranklist.RankSet) ([]string, poolRanksMap, error) {
	return svc.getPoolRanks(ctx, ranks, true)
}

func (svc *mgmtSvc) getPoolRanksDisabled(ctx context.Context, ranks *ranklist.RankSet) ([]string, poolRanksMap, error) {
	return svc.getPoolRanks(ctx, ranks, false)
}

type poolRanksOpSig func(context.Context, control.UnaryInvoker, *control.PoolRanksReq) (*control.PoolRanksResp, error)

// Generate operation results by iterating through pool's ranks and calling supplied fn on each.
func (svc *mgmtSvc) getPoolRanksResps(ctx context.Context, sys string, poolIDs []string, poolRanks poolRanksMap, ctlApiCall poolRanksOpSig) ([]*control.PoolRanksResp, error) {
	resps := []*control.PoolRanksResp{}

	_, replicas, err := svc.sysdb.LeaderQuery()
	if err != nil {
		return nil, err
	}

	for _, id := range poolIDs {
		rs := poolRanks[id]
		if rs.Count() == 0 {
			continue
		}

		req := &control.PoolRanksReq{
			ID:    id,
			Ranks: rs.Ranks(),
		}
		req.Sys = sys
		// Set request hostlist from leader query as we don't have
		// access to the server config from here.
		req.SetHostList(replicas)

		svc.log.Tracef("%T: %+v", req, req)

		resp, err := ctlApiCall(ctx, svc.rpcClient, req)
		if err != nil {
			return nil, errors.Wrapf(err, "%T", ctlApiCall)
		}

		svc.log.Tracef("%T: %+v", resp, resp)

		if resp == nil {
			return nil, errors.Errorf("nil %T", resp)
		}

		for _, res := range resp.Results {
			svc.log.Tracef("%T: %+v", res, res)
		}

		resps = append(resps, resp)
	}

	return resps, nil
}

// SystemDrain marks specified ranks on all pools as being in a drain state.
func (svc *mgmtSvc) SystemDrain(ctx context.Context, pbReq *mgmtpb.SystemDrainReq) (*mgmtpb.SystemDrainResp, error) {
	if pbReq == nil {
		return nil, errors.Errorf("nil %T", pbReq)
	}

	if err := svc.checkLeaderRequest(wrapCheckerReq(pbReq)); err != nil {
		return nil, err
	}

	// Validate requested hosts or ranks exist and fail if any are missing.
	hitRanks, err := svc.refuseUnavailableRanks(pbReq.Hosts, pbReq.Ranks)
	if err != nil {
		svc.log.Errorf("refuse unavailable ranks: %s", err)
		return nil, err
	}

	var poolIDs []string
	var poolRanks poolRanksMap
	var apiCall poolRanksOpSig

	// Retrieve rank-to-pool mappings. Enabled for drain, disabled for reintegrate.
	if pbReq.Reint {
		apiCall = control.PoolReintegrate
		poolIDs, poolRanks, err = svc.getPoolRanksDisabled(ctx, hitRanks)
	} else {
		apiCall = control.PoolDrain
		poolIDs, poolRanks, err = svc.getPoolRanksEnabled(ctx, hitRanks)
	}
	if err != nil {
		return nil, err
	}

	if len(poolIDs) != len(poolRanks) {
		return nil, errors.New("nr poolIDs should be equal to poolRanks keys")
	}
	if len(poolIDs) == 0 {
		return nil, errors.New("no pool-ranks found to operate on with request params")
	}

	// Generate results from dRPC calls to operate on pool ranks.
	resps, err := svc.getPoolRanksResps(ctx, pbReq.Sys, poolIDs, poolRanks, apiCall)
	if err != nil {
		return nil, err
	}

	if len(resps) == 0 {
		return nil, errors.New("no pool-ranks responses received")
	}
	if len(resps) != len(poolIDs) {
		return nil, errors.Errorf("unexpected number of pool-ranks responses received, "+
			"want %d got %d", len(poolIDs), len(resps))
	}

	pbResp := &mgmtpb.SystemDrainResp{}
	if err := convert.Types(resps, &pbResp.Responses); err != nil {
		return nil, errors.Wrapf(err, "convert %T->%T", resps, pbResp.Responses)
	}
	pbResp.Reint = pbReq.Reint

	return pbResp, nil
}

// SystemRebuildManage triggers pool-rebuild operations on all pools in system.
func (svc *mgmtSvc) SystemRebuildManage(ctx context.Context, pbReq *mgmtpb.SystemRebuildManageReq) (*mgmtpb.SystemRebuildManageResp, error) {
	if pbReq == nil {
		return nil, errors.Errorf("nil %T", pbReq)
	}

	if err := svc.checkLeaderRequest(pbReq); err != nil {
		return nil, err
	}

	poolIDs, err := svc.getPoolIDs()
	if err != nil {
		return nil, err
	}
	if len(poolIDs) == 0 {
		return &mgmtpb.SystemRebuildManageResp{}, nil // Successful no-op.
	}

	_, replicas, err := svc.sysdb.LeaderQuery()
	if err != nil {
		return nil, err
	}

	var results []*control.PoolRebuildManageResult
	for _, id := range poolIDs {
		opCode := control.PoolRebuildOpCode(pbReq.OpCode)

		req := &control.PoolRebuildManageReq{
			ID:     id,
			OpCode: opCode,
			Force:  pbReq.Force,
		}
		// Set request hostlist from leader query as we don't have
		// access to the server config from here.
		req.SetHostList(replicas)

		svc.log.Tracef("%T: %+v", req, req)

		result := &control.PoolRebuildManageResult{
			ID:     id,
			OpCode: opCode,
		}

		if err := control.PoolRebuildManage(ctx, svc.rpcClient, req); err != nil {
			result.Errored = true
			result.Msg = err.Error()
		}

		svc.log.Tracef("%T: %+v", result, result)
		results = append(results, result)
	}

	pbResp := &mgmtpb.SystemRebuildManageResp{}
	if err := convert.Types(results, &pbResp.Results); err != nil {
		return nil, errors.Wrapf(err, "convert %T->%T", results, pbResp.Results)
	}

	return pbResp, nil
}

// selfHealExcludeRanks fetches a list of detected dead ranks from the leader's engine and updates
// states within the control-plane membership appropriately.
func (svc *mgmtSvc) selfHealExcludeRanks(ctx context.Context) error {
	// DAOS-18163 TODO: Pass a real, nonzero map version.
	req := &mgmtpb.GetGroupStatusReq{}

	// Fetch dead rank list from leader's engine with group status dRPC call.
	dResp, err := svc.harness.CallDrpc(ctx, daos.MethodGroupStatusGet, req)
	if err != nil {
		if err == errEngineNotReady {
			return err
		}
		svc.log.Errorf("dRPC GroupStatusGet call failed: %s", err)
		return err
	}

	resp := new(mgmtpb.GetGroupStatusResp)
	if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
		return err
	}
	if resp.GetStatus() != 0 {
		return daos.Status(resp.GetStatus())
	}

	svc.log.Tracef("excluding ranks based on self_heal: %T:%+v", resp, resp)

	if len(resp.DeadRanks) > 0 {
		svc.log.Debugf("dead ranks %s returned from get-group-status drpc",
			ranklist.RankSetFromRanks(ranklist.RanksFromUint32(resp.DeadRanks)))
	}
	for _, deadRank := range resp.DeadRanks {
		// No incarnation to verify here so pass zero to skip it's check.
		needsGrpUpd, err := svc.membership.MarkRankDead(ranklist.Rank(deadRank), 0)
		if system.IsMemberNotFound(err) {
			svc.log.Debugf("MarkRankDead: rank %d not found", deadRank)
			continue
		}
		if err != nil {
			return err
		}
		if needsGrpUpd {
			svc.log.Debugf("do group update after marking rank %d dead", deadRank)
			svc.reqGroupUpdate(ctx, false)
		}
	}

	return nil
}

// selfHealNotifyPSes calls into each pool service with the current value of self_heal system
// property so that the appropriate actions can be performed across the pool (e.g. rebuild or
// exclude).
func (svc *mgmtSvc) selfHealNotifyPSes(ctx context.Context, propVal string) error {
	poolIDs, err := svc.getPoolIDs()
	if err != nil {
		return err
	}
	svc.log.Tracef("evaluating self_heal sys-prop %q on %d pools", propVal, len(poolIDs))

	if len(poolIDs) == 0 {
		return nil // Successful no-op.
	}

	_, replicas, err := svc.sysdb.LeaderQuery()
	if err != nil {
		return err
	}

	var successes, failures []string
	for _, id := range poolIDs {
		req := &control.PoolSelfHealEvalReq{
			ID:         id,
			SysPropVal: propVal,
		}
		// Set request hostlist from leader query as we don't have
		// access to the server config from here.
		req.SetHostList(replicas)

		svc.log.Tracef("%T: %+v", req, req)

		if err := control.PoolSelfHealEval(ctx, svc.rpcClient, req); err != nil {
			failures = append(failures, id)
			svc.log.Errorf("PoolSelfHealEval: %s", err.Error())
		} else {
			successes = append(successes, id)
		}
	}

	if len(successes) > 0 {
		svc.log.Debugf("PoolSelfHealEval completed on %s (%s)",
			english.Plural(len(successes), "pool", "pools"),
			strings.Join(successes, ", "))
	}
	if len(failures) > 0 {
		return errors.Errorf(
			"pool self-heal evaluate drpc failed for %s (%s), check server log",
			english.Plural(len(failures), "pool", "pools"),
			strings.Join(failures, ", "))
	}

	return nil
}

// SystemSelfHealEval triggers actions based on self_heal system property values.
func (svc *mgmtSvc) SystemSelfHealEval(ctx context.Context, pbReq *mgmtpb.SystemSelfHealEvalReq) (*mgmtpb.DaosResp, error) {
	if pbReq == nil {
		return nil, errors.Errorf("nil %T", pbReq)
	}

	if err := svc.checkLeaderRequest(pbReq); err != nil {
		return nil, err
	}

	// Retrieve system self-heal property. Assume a system property exists when running eval.
	selfHeal, err := svc.getSysSelfHeal()
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("system property self_heal='%+v'", selfHeal)

	// Exclude engines based on SWIM status if system property bit set.
	if daos.SystemPropertySelfHealHasFlag(selfHeal, daos.SysSelfHealFlagExclude) {
		if err := svc.selfHealExcludeRanks(ctx); err != nil {
			return nil, errors.Wrap(err, "excluding ranks based on self_heal.exclude")
		}
	}

	// If pool_exclude or pool_rebuild is set, send the latest self_heal value to all PSs, who
	// will handle the reevaluation. This involves calling into the leader engine with self_heal
	// value for each pool and calling dsc_pool_svc_eval_self_heal() in dRPC handler.
	if !daos.SystemPropertySelfHealHasFlag(selfHeal, daos.SysSelfHealFlagPoolRebuild) &&
		!daos.SystemPropertySelfHealHasFlag(selfHeal, daos.SysSelfHealFlagPoolExclude) {
		return new(mgmtpb.DaosResp), nil
	}
	if err := svc.selfHealNotifyPSes(ctx, selfHeal); err != nil {
		return nil, errors.Wrapf(err, "notify pool services of self_heal=%q", selfHeal)
	}

	return new(mgmtpb.DaosResp), nil
}

// ClusterEvent management service gRPC handler receives ClusterEvent requests
// from control-plane instances attempting to notify the MS of a cluster event
// in the DAOS system (this handler should only get called on the MS leader).
func (svc *mgmtSvc) ClusterEvent(ctx context.Context, req *sharedpb.ClusterEventReq) (*sharedpb.ClusterEventResp, error) {
	if err := svc.checkLeaderRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	// indicate to handler that event has been forwarded
	resp, err := svc.events.HandleClusterEvent(req, true)
	if err != nil {
		return nil, errors.Wrapf(err, "handle cluster event %+v", req)
	}

	return resp, nil
}

// awaitSync issues a bulk filesystem sync, a cheap best-effort flush of any pending
// writes unrelated to the removals performed by the caller (e.g. engine I/O prior to
// being killed). It does not by itself guarantee that a specific removal has been
// committed to disk; callers that need that guarantee should follow up with fsyncDir()
// on the relevant parent directory, which deterministically blocks until the directory
// entry change is durable, rather than sleeping an arbitrary duration.
func awaitSync() {
	unix.Sync()
}

// fsyncDir opens path and fsyncs it, blocking until directory-entry changes made
// within it (e.g. a file or subdirectory removal) are committed to stable storage.
func fsyncDir(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()

	return f.Sync()
}

// eraseSysdb is called on MS replicas to shut down the raft DB and remove its files.
// The caller (SystemErase()) restarts the control plane afterwards via a deferred call
// to scheduleControlPlaneRestart().
func (svc *mgmtSvc) eraseSysdb(errOnFail bool, leaderStr string) error {
	pid := os.Getpid()
	svc.log.Infof("[%s] pid %d: erasing system db", leaderStr, pid)

	if err := svc.sysdb.Stop(); err != nil {
		if errOnFail {
			return errors.Wrapf(err, "failed to stop system database on pid %d [role=%s]",
				pid, leaderStr)
		}
		// Log but continue - RemoveFiles may still work after subsequent process restart
		svc.log.Errorf("stop system database: %s (continuing with erase), pid %d [role=%s]",
			err, pid, leaderStr)
	}

	raftDir := svc.sysdb.RaftDir()
	// RemoveFiles() renames RaftDir aside before removing it, so even if it returns an
	// error here (e.g. a transient failure removing the renamed copy), RaftDir itself is
	// already gone and the control plane restart scheduled by our caller's defer will
	// bootstrap a fresh, empty database rather than reloading stale pre-erase raft state.
	if err := svc.sysdb.RemoveFiles(); err != nil {
		return errors.Wrapf(err, "failed to remove system database on pid %d [role=%s]",
			pid, leaderStr)
	}

	awaitSync()

	// Deterministically confirm the raft dir removal is durable before restarting,
	// rather than sleeping an arbitrary duration after the bulk sync() above.
	if err := fsyncDir(filepath.Dir(raftDir)); err != nil {
		svc.log.Errorf("fsync raft dir parent %q: %s, pid %d [role=%s]",
			filepath.Dir(raftDir), err, pid, leaderStr)
	}

	return nil
}

// resetLocalEngines stops this host's engines, wipes their superblocks and (in
// MD-on-SSD mode) the control-metadata directory, so they come up in AwaitFormat
// state after the control plane restarts. Called on both the leader (for its own
// ranks) and replicas (via a forwarded SystemErase RPC).
func (svc *mgmtSvc) resetLocalEngines() error {
	svc.log.Trace("SystemErase: REPLICA - Step 1: Stopping local engines")

	instances := svc.harness.Instances()
	sbDirs := make(map[string]struct{}) // unique parent dirs of removed superblocks
	for _, engine := range instances {
		svc.log.Tracef("SystemErase: REPLICA - Stopping engine instance %d", engine.Index())
		if err := engine.Stop(unix.SIGKILL); err != nil {
			svc.log.Errorf("instance %d failed to stop: %s", engine.Index(), err)
		}
		svc.log.Tracef("SystemErase: REPLICA - Removing superblock for instance %d", engine.Index())
		if err := engine.RemoveSuperblock(); err != nil {
			svc.log.Errorf("instance %d failed to remove superblock: %s", engine.Index(), err)
		}
		if storage := engine.GetStorage(); storage != nil {
			sbDirs[storage.ControlMetadataEnginePath()] = struct{}{}
		}
	}

	svc.log.Trace("SystemErase: REPLICA - Step 2: Syncing filesystem")

	awaitSync()

	// Deterministically confirm each superblock removal is durable before proceeding,
	// rather than sleeping an arbitrary duration after the bulk sync() above.
	for dir := range sbDirs {
		if err := fsyncDir(dir); err != nil {
			svc.log.Errorf("fsync superblock dir %q: %s", dir, err)
		}
	}

	// Remove the control-metadata directory once (not per-engine, to avoid spurious
	// "directory not found" errors when multiple local engines race on this). Without
	// this a restarted replica reports "Metadata format required" instead of the
	// expected "SCM format required".
	if len(instances) > 0 {
		if storage := instances[0].GetStorage(); storage != nil && storage.ControlMetadataPathConfigured() {
			mdPath := storage.ControlMetadataPath()
			svc.log.Tracef("SystemErase: REPLICA - Removing entire control metadata directory: %s", mdPath)
			if err := os.RemoveAll(mdPath); err != nil {
				svc.log.Errorf("failed to remove control metadata directory %q: %s", mdPath, err)
			} else {
				awaitSync()
				if err := fsyncDir(filepath.Dir(mdPath)); err != nil {
					svc.log.Errorf("fsync control metadata parent dir %q: %s", filepath.Dir(mdPath), err)
				}
			}
		}
	}

	return nil
}

// getPeersAndFanout gathers MS replica peer addresses and prepares a ResetFormatRanks
// fanout request targeting every rank except those on the leader or its replica peers
// (which erase and restart themselves instead; see resetLocalEngines()/eraseReplicas()).
// Must be called before the raft DB is stopped, as rank/host resolution needs it.
func (svc *mgmtSvc) getPeersAndFanout(ctx context.Context) ([]*net.TCPAddr, *fanoutRequest, *fanoutResponse, error) {
	svc.log.Trace("SystemErase: LEADER - Step 1: Gathering MS replica peer addresses")
	peers, err := svc.sysdb.PeerAddrs()
	if err != nil {
		return nil, nil, nil, err
	}
	svc.log.Tracef("SystemErase: LEADER - Found %d MS replica peer(s): %v", len(peers), peers)

	svc.log.Trace("SystemErase: LEADER - Step 2: Preparing fanout request to be used later")
	// Pass empty req to request operation across all membership ranks
	fanReq, fanResp, err := svc.getFanout(&mgmtpb.SystemQueryReq{})
	if err != nil {
		return nil, nil, nil, err
	}

	selfAddr, err := svc.sysdb.ReplicaAddr()
	if err != nil {
		return nil, nil, nil, err
	}
	selfAndPeerHosts := make([]string, 0, len(peers)+1)
	selfAndPeerHosts = append(selfAndPeerHosts, selfAddr.String())
	for _, peer := range peers {
		selfAndPeerHosts = append(selfAndPeerHosts, peer.String())
	}

	excludeRanks, _, err := svc.membership.CheckHosts(strings.Join(selfAndPeerHosts, ","),
		build.DefaultControlPort)
	if err != nil {
		return nil, nil, nil, err
	}
	for _, rank := range excludeRanks.Ranks() {
		fanReq.Ranks.Delete(rank)
	}
	svc.log.Tracef("SystemErase: LEADER - Fanout request prepared for non-leader, non-replica ranks: %s",
		fanReq.Ranks)

	return peers, fanReq, fanResp, nil
}

// eraseReplicas sends erase requests to all MS replica peers, each of which erases its
// own DB/engines synchronously before acking (see resetLocalEngines()) and then restarts
// itself. No follow-up poll is needed or attempted: by the time this returns without
// error, every peer is provably inactive with a wiped sysdb, restarted or not.
func (svc *mgmtSvc) eraseReplicas(ctx context.Context, peers []*net.TCPAddr) error {
	svc.log.Trace("SystemErase: LEADER - Step 4: Sending erase request to MS replica peers")

	for _, peer := range peers {
		svc.log.Tracef("SystemErase: LEADER - Sending erase RPC to peer %s", peer.String())
		peerReq := new(control.SystemEraseReq)
		peerReq.AddHost(peer.String())

		if _, err := control.SystemErase(ctx, svc.rpcClient, peerReq); err != nil {
			if control.IsRetryableConnErr(err) {
				svc.log.Tracef("SystemErase: LEADER - Retryable connection error for peer %s: %s",
					peer.String(), err)
				continue
			}
			return err
		}
		svc.log.Tracef("SystemErase: LEADER - Peer %s acknowledged erase request", peer.String())
	}

	return nil
}

// wipeEngineSuperblocks calls ResetFormatRanks on non-leader, non-replica engines (see
// getPeersAndFanout()) to wipe their superblocks and bring them up in AwaitFormat state.
//
// This deliberately does not wait for a new raft leader to be elected first: only the
// bootstrap MS replica can form a new single-node raft cluster after an erase, and it
// can't do so until it restarts -- which can't happen until this function (running on
// the pre-erase leader) returns. Waiting here for a new leader would be a circular
// deadlock.
func (svc *mgmtSvc) wipeEngineSuperblocks(ctx context.Context, fanReq *fanoutRequest, fanResp *fanoutResponse) (*mgmtpb.SystemEraseResp, error) {
	svc.log.Trace("SystemErase: LEADER - Step 5: Calling ResetFormatRanks on non-replica engines")

	fanReq.Method = control.ResetFormatRanks

	var err error
	fanResp, _, err = svc.rpcFanout(ctx, fanReq, fanResp, false)
	if err != nil {
		return nil, err
	}

	for _, mr := range fanResp.Results {
		svc.log.Tracef("SystemErase: LEADER - ResetFormatRanks result: %#v", mr)
	}

	pbResp := new(mgmtpb.SystemEraseResp)
	if err := convert.Types(fanResp.Results, &pbResp.Results); err != nil {
		return nil, err
	}
	for _, result := range pbResp.Results {
		result.Action = "reset format"
	}

	if fanResp.Results.Errors() != nil {
		svc.log.Errorf("SystemErase: LEADER - ResetFormatRanks encountered errors")
		return pbResp, nil
	}
	svc.log.Info("System Erase complete, engines ready for manual 'dmg storage format'")

	return pbResp, nil
}

// resetAllEngines runs on the leader to coordinate the erase across replicas and engines.
func (svc *mgmtSvc) resetAllEngines(ctx context.Context) (*mgmtpb.SystemEraseResp, error) {
	peers, fanReq, fanResp, err := svc.getPeersAndFanout(ctx)
	if err != nil {
		return nil, errors.Wrap(err, "unable to determine path to self")
	}

	// Erase the leader's own DB before telling replicas to erase, to prevent a race
	// where a replica restarts with a clean DB, rejoins, and syncs the leader's stale
	// entries before the leader erases itself. The leader doesn't restart yet -- it
	// stays up to coordinate replica/engine erase below.
	//
	// SAFETY WARNING: once raft is stopped the leader cannot handle leadership changes;
	// if it crashes before completing, manual recovery will be required. (POINT OF NO
	// RETURN)
	svc.log.Trace("SystemErase: LEADER - Step 3: Erasing leader's raft DB (POINT OF NO RETURN)")

	if err := svc.eraseSysdb(false, "LEADER"); err != nil {
		return nil, err
	}

	if err := svc.eraseReplicas(ctx, peers); err != nil {
		return nil, err
	}

	if err := svc.resetLocalEngines(); err != nil {
		return nil, err
	}

	// MS-leader and replicas have been handled, now non-MS hosts
	pbResp, err := svc.wipeEngineSuperblocks(ctx, fanReq, fanResp)
	if err != nil {
		return nil, err
	}

	return pbResp, nil
}

// postGracefulStopSettleDelay is a pragmatic (not guaranteed) mitigation for a race in
// scheduleControlPlaneRestart(): grpcServer.GracefulStop() returning only means each RPC
// handler's response has been handed off to grpc-go's async per-connection write
// goroutine, not that it has actually hit the wire. exec() then immediately closes every
// socket, which can race ahead of that pending write and truncate the response the client
// is waiting for. This delay gives the writer goroutine a window to flush first.
const postGracefulStopSettleDelay = 250 * time.Millisecond

// scheduleControlPlaneRestart schedules the control plane process to restart after a delay.
// Called from a defer so the restart happens after the gRPC response is sent: it drains the
// gRPC server via GracefulStop() (falling back to a forced Stop() after
// gracefulStopTimeout), waits postGracefulStopSettleDelay for the response write to flush
// (see above), then exec()s.
func (svc *mgmtSvc) scheduleControlPlaneRestart(leaderStr string) error {
	myPath, err := os.Readlink("/proc/self/exe")
	if err != nil {
		return errors.Wrap(err, "unable to determine path to self")
	}

	svc.log.Infof("System Erase: scheduling control plane restart [role=%s]", leaderStr)

	// Drain in-flight gRPC calls (in particular, this handler's own response) via
	// GracefulStop() before restarting, since exec() replaces the process image and
	// closes every socket with no guarantee that queued response bytes have been
	// written yet. A bounded fallback timeout guards against long-lived RPCs (e.g.
	// event subscriptions) preventing the drain from ever completing.
	go func() {
		scheduledAt := time.Now()
		if svc.grpcServer != nil {
			done := make(chan struct{})
			go func() {
				svc.grpcServer.GracefulStop()
				close(done)
			}()

			timeout := svc.gracefulStopTimeout
			if timeout <= 0 {
				timeout = defaultGracefulStopTimeout
			}

			select {
			case <-done:
				svc.log.Debugf("System Erase: gRPC server drained cleanly after %s [role=%s]",
					time.Since(scheduledAt), leaderStr)
			case <-time.After(timeout):
				svc.log.Errorf("System Erase: timed out after %s waiting for gRPC server to drain, forcing stop [role=%s]",
					timeout, leaderStr)
				svc.grpcServer.Stop()
			}

			svc.log.Debugf("System Erase: applying %s post-drain settle delay before exec [role=%s]",
				postGracefulStopSettleDelay, leaderStr)
			time.Sleep(postGracefulStopSettleDelay)
		} else {
			// No grpcServer wired up (e.g. unit tests); fall back to a short
			// delay as a best-effort substitute.
			time.Sleep(500 * time.Millisecond)
		}

		svc.log.Infof("System Erase: exec'ing %s to restart control plane after %s total drain [role=%s]",
			myPath, time.Since(scheduledAt), leaderStr)
		if err := exec(myPath, append([]string{myPath}, os.Args[1:]...), os.Environ()); err != nil {
			svc.log.Error(errors.Wrap(err, "Exec() failed").Error())
		}
	}()

	return nil
}

// SystemErase implements the gRPC handler for erasing system metadata.
//
// SAFETY WARNINGS:
//  1. This operation is DESTRUCTIVE and IRREVERSIBLE
//  2. All ranks must be stopped before calling (enforced by checkSystemErase)
//  3. On the leader, raft DB is stopped early in the operation, creating a window
//     where leadership changes cannot be safely handled
//  4. If the leader crashes after stopping raft but before completing, manual
//     recovery will be required (replicas/engines may be in inconsistent states)
//  5. The operation should complete in under 1 minute under normal conditions
//
// Operation sequence:
// - Non-leader replicas: erase local engines + DB, restart immediately
// - Leader: erase its own DB first, coordinate replica/engine erase, restart last
func (svc *mgmtSvc) SystemErase(ctx context.Context, pbReq *mgmtpb.SystemEraseReq) (*mgmtpb.SystemEraseResp, error) {
	// Distinguish an externally-initiated request (e.g. from the dmg admin tool) from
	// an internally-forwarded request sent by the MS leader directly to a specific
	// non-leader replica peer (see eraseReplicas()). Only external requests must be
	// routed to the current leader so it can coordinate DB/engine erasure across all
	// replicas and ranks; forwarded per-replica requests are explicitly targeted at a
	// non-leader host by the leader and must be allowed to run locally there.
	//
	// If the caller's component can't be determined (e.g. missing header in an
	// insecure/no-TLS deployment, or an unexpected error), fail safe by treating the
	// request as external so that leader-only enforcement still applies.
	isForwarded := false
	if comp, err := build.FromContext(ctx); err == nil && comp.Component == build.ComponentServer {
		isForwarded = true
	}

	if isForwarded {
		// At a minimum, ensure that this only runs on MS replicas.
		if err := svc.checkReplicaRequest(pbReq); err != nil {
			return nil, err
		}
	} else {
		// External (admin) request: must be serviced by the current leader. If this
		// replica isn't the leader, checkLeaderRequest() returns *system.ErrNotLeader,
		// which the dmg/control client automatically retries against the leader hint
		// (see lib/control/rpc.go's *system.ErrNotLeader handling).
		if err := svc.checkLeaderRequest(pbReq); err != nil {
			return nil, err
		}
	}

	isLeader := svc.sysdb.IsLeader()
	leaderStr := "REPLICA"
	if isLeader {
		leaderStr = "LEADER"
	}
	svc.log.Tracef("SystemErase: START [role=%s]", leaderStr)

	// Restart applies to both leader and replicas; deferred so it fires after the
	// response is sent (see scheduleControlPlaneRestart()).
	defer func() {
		if err := svc.scheduleControlPlaneRestart(leaderStr); err != nil {
			svc.log.Errorf("failed to schedule control plane restart: %s", err)
		}
	}()

	if !isLeader {
		svc.log.Trace("SystemErase: REPLICA - Step 3: Erasing raft DB")
		if err := svc.eraseSysdb(true, leaderStr); err != nil {
			return nil, errors.Wrap(err, "erasing non-leader ms-replica db")
		}

		if err := svc.resetLocalEngines(); err != nil {
			return nil, errors.Wrap(err, "resetting non-leader ms-replica engines")
		}

		svc.log.Tracef("SystemErase: REPLICA - returning success to caller [isForwarded=%v]", isForwarded)
		return &mgmtpb.SystemEraseResp{}, nil
	}

	pbResp, err := svc.resetAllEngines(ctx)
	if err != nil {
		svc.log.Errorf("SystemErase: LEADER - returning error to caller: %s", err)
	} else {
		svc.log.Tracef("SystemErase: LEADER - returning success to caller: %#v", pbResp)
	}
	return pbResp, err
}

// SystemCleanup implements the method defined for the Management Service.
//
// Signal to the data plane to find all resources associated with a given machine
// and release them. This includes releasing all container and pool handles associated
// with the machine.
func (svc *mgmtSvc) SystemCleanup(ctx context.Context, req *mgmtpb.SystemCleanupReq) (*mgmtpb.SystemCleanupResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	if req.Machine == "" {
		return nil, errors.New("SystemCleanup requires a machine name.")
	}

	psList, err := svc.sysdb.PoolServiceList(false)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.SystemCleanupResp)

	for _, ps := range psList {
		var errMsg string

		evictReq := &mgmtpb.PoolEvictReq{
			Sys:     req.Sys,
			Machine: req.Machine,
			Id:      ps.PoolUUID.String(),
		}

		dResp, err := svc.makePoolServiceCall(ctx, daos.MethodPoolEvict, evictReq)
		if err != nil {
			return nil, err
		}

		evictResp := &mgmtpb.PoolEvictResp{}
		if err := svc.unmarshalPB(dResp.Body, evictResp); err != nil {
			evictResp.Status = int32(daos.MiscError)
			evictResp.Count = 0
			errMsg = err.Error()
		} else if evictResp.Status != int32(daos.Success) {
			errMsg = fmt.Sprintf("Unable to clean up handles for machine %s on pool %s",
				evictReq.Machine, evictReq.Id)
		}

		svc.log.Debugf("Response from pool evict in cleanup: '%+v' (req: '%+v')", evictResp,
			evictReq)
		resp.Results = append(resp.Results, &mgmtpb.SystemCleanupResp_CleanupResult{
			Status: evictResp.Status,
			Msg:    errMsg,
			PoolId: evictReq.Id,
			Count:  uint32(evictResp.Count),
		})
	}

	return resp, nil
}

// SystemSetAttr sets system-level attributes.
func (svc *mgmtSvc) SystemSetAttr(ctx context.Context, req *mgmtpb.SystemSetAttrReq) (_ *mgmtpb.DaosResp, err error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

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

	props, err := system.GetAttributes(svc.sysdb, req.GetKeys())
	if err != nil {
		return nil, err
	}

	resp = &mgmtpb.SystemGetAttrResp{Attributes: props}
	return
}

func sp2pp(sp *daos.SystemProperty) (*daos.PoolProperty, bool) {
	if pp, ok := sp.Value.(interface{ PoolProperty() *daos.PoolProperty }); ok {
		return pp.PoolProperty(), true
	}
	return nil, false
}

// SystemSetProp sets user-visible system properties.
func (svc *mgmtSvc) SystemSetProp(ctx context.Context, req *mgmtpb.SystemSetPropReq) (*mgmtpb.DaosResp, error) {
	if err := svc.checkLeaderRequest(req); err != nil {
		return nil, err
	}

	if err := system.SetUserProperties(svc.sysdb, svc.systemProps, req.GetProperties()); err != nil {
		return nil, err
	}

	if err := svc.updatePoolPropsWithSysProps(ctx, req.GetProperties(), req.Sys); err != nil {
		return nil, err
	}

	// Indicate success.
	return new(mgmtpb.DaosResp), nil
}

// updatePoolPropsWithSysProps This function will take systemProperties and
// update each associated pool property (if one exists) on each pool
func (svc *mgmtSvc) updatePoolPropsWithSysProps(ctx context.Context, systemProperties map[string]string, sys string) error {
	// Get the properties from the request, convert to pool prop, then put into poolSysProps
	var poolSysProps []*daos.PoolProperty
	for k, v := range systemProperties {
		p, ok := svc.systemProps.Get(k)
		if !ok {
			return errors.Errorf("unknown property %q", k)
		}
		if pp, ok := sp2pp(p); ok {
			if err := pp.SetValue(v); err != nil {
				return errors.Wrapf(err, "invalid value %q for property %q", v, k)
			}
			poolSysProps = append(poolSysProps, pp)
		}
	}

	if len(poolSysProps) == 0 {
		return nil
	}

	// Create the request for updating the pools. The request will have all pool properties
	pspr := &mgmtpb.PoolSetPropReq{
		Sys:        sys,
		Properties: make([]*mgmtpb.PoolProperty, len(poolSysProps)),
	}
	for i, p := range poolSysProps {
		pspr.Properties[i] = &mgmtpb.PoolProperty{
			Number: p.Number,
		}
		if nv, err := p.Value.GetNumber(); err == nil {
			pspr.Properties[i].SetValueNumber(nv)
		} else {
			pspr.Properties[i].SetValueString(p.Value.String())
		}
	}

	pools, err := svc.sysdb.PoolServiceList(false)
	if err != nil {
		return err
	}
	for _, ps := range pools {
		pspr.Id = ps.PoolUUID.String()
		pspr.SvcRanks = ranklist.RanksToUint32(ps.Replicas)
		dResp, err := svc.makePoolServiceCall(ctx, daos.MethodPoolSetProp, pspr)
		if err != nil {
			return err
		}

		resp := new(mgmtpb.DaosResp)
		if err := svc.unmarshalPB(dResp.Body, resp); err != nil {
			return err
		}
		if resp.Status != 0 {
			return errors.Errorf("SystemSetProp: %d\n", resp.Status)
		}
	}

	return nil
}

// SystemGetProp gets user-visible system properties.
func (svc *mgmtSvc) SystemGetProp(ctx context.Context, req *mgmtpb.SystemGetPropReq) (*mgmtpb.SystemGetPropResp, error) {
	if err := svc.checkReplicaRequest(wrapCheckerReq(req)); err != nil {
		return nil, err
	}

	props, err := system.GetUserProperties(svc.sysdb, svc.systemProps, req.GetKeys())
	if err != nil {
		return nil, err
	}

	return &mgmtpb.SystemGetPropResp{Properties: props}, nil
}
