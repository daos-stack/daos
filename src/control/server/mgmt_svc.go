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
	"time"

	"github.com/golang/protobuf/proto"
	uuid "github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/system"
)

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

// mgmtSvc implements (the Go portion of) Management Service, satisfying
// mgmtpb.MgmtSvcServer.
type mgmtSvc struct {
	log              logging.Logger
	harness          *IOServerHarness
	membership       *system.Membership // if MS leader, system membership list
	sysdb            *system.Database
	events           *events.PubSub
	clientNetworkCfg *config.ClientNetworkCfg
	joinReqs         joinReqChan
}

func newMgmtSvc(h *IOServerHarness, m *system.Membership, s *system.Database, p *events.PubSub) *mgmtSvc {
	return &mgmtSvc{
		log:              h.log,
		harness:          h,
		membership:       m,
		sysdb:            s,
		events:           p,
		clientNetworkCfg: new(config.ClientNetworkCfg),
		joinReqs:         make(joinReqChan),
	}
}

// checkSystemRequest sanity checks that a request is not nil and
// has been sent to the correct system.
func (svc *mgmtSvc) checkSystemRequest(req proto.Message) error {
	if common.InterfaceIsNil(req) {
		return errors.New("nil request")
	}
	if sReq, ok := req.(interface{ GetSys() string }); ok {
		if sReq.GetSys() != svc.sysdb.SystemName() {
			return FaultWrongSystem(sReq.GetSys(), svc.sysdb.SystemName())
		}
	}
	return nil
}

// checkLeaderRequest performs sanity-checking on a request that must
// be run on the current MS leader.
func (svc *mgmtSvc) checkLeaderRequest(req proto.Message) error {
	if err := svc.sysdb.CheckLeader(); err != nil {
		return err
	}
	return svc.checkSystemRequest(req)
}

// checkReplicaRequest performs sanity-checking on a request that must
// be run on a MS replica.
func (svc *mgmtSvc) checkReplicaRequest(req proto.Message) error {
	if err := svc.sysdb.CheckReplica(); err != nil {
		return err
	}
	return svc.checkSystemRequest(req)
}

// GetAttachInfo handles a request to retrieve a map of ranks to fabric URIs, in addition
// to client network autoconfiguration hints.
//
// The default use case is for libdaos clients to obtain the set of ranks associated
// with MS replicas.
func (svc *mgmtSvc) GetAttachInfo(ctx context.Context, req *mgmtpb.GetAttachInfoReq) (*mgmtpb.GetAttachInfoResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	if svc.clientNetworkCfg == nil {
		return nil, errors.New("clientNetworkCfg is missing")
	}
	svc.log.Debugf("MgmtSvc.GetAttachInfo dispatch, req:%+v\n", *req)

	var groupMap *system.GroupMap
	var err error
	if req.GetAllRanks() {
		groupMap, err = svc.sysdb.GroupMap()
	} else {
		groupMap, err = svc.sysdb.ReplicaRanks()
	}
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.GetAttachInfoResp)
	for rank, uri := range groupMap.RankURIs {
		resp.Psrs = append(resp.Psrs, &mgmtpb.GetAttachInfoResp_Psr{
			Rank: rank.Uint32(),
			Uri:  uri,
		})
	}
	resp.Provider = svc.clientNetworkCfg.Provider
	resp.CrtCtxShareAddr = svc.clientNetworkCfg.CrtCtxShareAddr
	resp.CrtTimeout = svc.clientNetworkCfg.CrtTimeout
	resp.NetDevClass = svc.clientNetworkCfg.NetDevClass

	svc.log.Debugf("MgmtSvc.GetAttachInfo dispatch, resp:%+v\n", *resp)
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
