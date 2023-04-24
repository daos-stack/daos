//
// (C) Copyright 2018-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"strings"
	"time"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

type (
	batchRequest struct {
		msg    proto.Message
		ctx    context.Context
		respCh batchRespChan
	}

	batchResponse struct {
		msg proto.Message
		err error
	}

	batchRespChan chan *batchResponse
	batchReqChan  chan *batchRequest

	batchProcessResp struct {
		msgName       string
		retryableReqs []*batchRequest
	}

	batchProcessRespChan chan *batchProcessResp
)

func (br *batchRequest) sendResponse(parent context.Context, msg proto.Message, err error) {
	select {
	case <-parent.Done():
		return
	case <-br.ctx.Done():
		return
	case br.respCh <- &batchResponse{msg: msg, err: err}:
	}
}

// mgmtSvc implements (the Go portion of) Management Service, satisfying
// mgmtpb.MgmtSvcServer.
type mgmtSvc struct {
	mgmtpb.UnimplementedMgmtSvcServer
	log               logging.Logger
	harness           *EngineHarness
	membership        *system.Membership // if MS leader, system membership list
	sysdb             *raft.Database
	rpcClient         control.UnaryInvoker
	events            *events.PubSub
	systemProps       daos.SystemPropertyMap
	clientNetworkHint *mgmtpb.ClientNetHint
	joinReqs          joinReqChan
	batchReqs         batchReqChan
	groupUpdateReqs   chan bool
	lastMapVer        uint32
}

func newMgmtSvc(h *EngineHarness, m *system.Membership, s *raft.Database, c control.UnaryInvoker, p *events.PubSub) *mgmtSvc {
	return &mgmtSvc{
		log:               h.log,
		harness:           h,
		membership:        m,
		sysdb:             s,
		rpcClient:         c,
		events:            p,
		systemProps:       daos.SystemProperties(),
		clientNetworkHint: new(mgmtpb.ClientNetHint),
		joinReqs:          make(joinReqChan),
		batchReqs:         make(batchReqChan),
		groupUpdateReqs:   make(chan bool),
	}
}

// checkSystemRequest sanity checks that a request is not nil and
// has been sent to the correct system.
func (svc *mgmtSvc) checkSystemRequest(req proto.Message) error {
	if common.InterfaceIsNil(req) {
		return errors.New("nil request")
	}
	if sReq, ok := req.(interface{ GetSys() string }); ok {
		comps := strings.Split(sReq.GetSys(), "-")
		sysName := comps[0]
		if len(comps) > 1 {
			if _, err := build.NewVersion(comps[len(comps)-1]); err == nil {
				sysName = strings.Join(comps[:len(comps)-1], "-")
			} else {
				sysName = strings.Join(comps, "-")
			}
		}

		if sysName != svc.sysdb.SystemName() {
			return FaultWrongSystem(sysName, svc.sysdb.SystemName())
		}
	}
	return nil
}

// checkLeaderRequest performs sanity-checking on a request that must
// be run on the current MS leader.
func (svc *mgmtSvc) checkLeaderRequest(req proto.Message) error {
	unwrapped, err := svc.unwrapCheckerReq(req)
	if err != nil {
		return err
	}
	req = unwrapped

	if err := svc.sysdb.CheckLeader(); err != nil {
		return err
	}
	return svc.checkSystemRequest(req)
}

// checkReplicaRequest performs sanity-checking on a request that must
// be run on a MS replica.
func (svc *mgmtSvc) checkReplicaRequest(req proto.Message) error {
	unwrapped, err := svc.unwrapCheckerReq(req)
	if err != nil {
		return err
	}
	req = unwrapped

	if err := svc.sysdb.CheckReplica(); err != nil {
		return err
	}
	return svc.checkSystemRequest(req)
}

// startBatchLoops kicks off the asynchronous batch processing loops.
func (svc *mgmtSvc) startBatchLoops(ctx context.Context) {
	go svc.joinLoop(ctx)
	go svc.batchLoop(ctx)
}

// submitBatchRequest submits a message for batch processing and waits for a response.
func (svc *mgmtSvc) submitBatchRequest(ctx context.Context, msg proto.Message) (proto.Message, error) {
	respCh := make(batchRespChan, 1)
	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case svc.batchReqs <- &batchRequest{msg: msg, ctx: ctx, respCh: respCh}:
	}

	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case resp := <-respCh:
		return resp.msg, resp.err
	}
}

// processBatchPoolEvictions processes a batch of PoolEvictReq messages,
// consolidating them by pool ID in order to minimize the number of
// dRPC calls to the engine.
func (svc *mgmtSvc) processBatchPoolEvictions(ctx context.Context, bprChan batchProcessRespChan, reqs []*batchRequest) []*batchRequest {
	poolReqs := make(map[string][]*batchRequest)

	for _, req := range reqs {
		msg, ok := req.msg.(*mgmtpb.PoolEvictReq)
		if !ok {
			svc.log.Errorf("unexpected message type %T", req.msg)
			continue
		}
		poolReqs[msg.Id] = append(poolReqs[msg.Id], req)
	}

	for poolID, reqs := range poolReqs {
		if len(reqs) == 0 {
			continue
		}

		batchReq := &mgmtpb.PoolEvictReq{Id: poolID, Sys: reqs[0].msg.(*mgmtpb.PoolEvictReq).Sys}
		for _, req := range reqs {
			batchReq.Handles = append(batchReq.Handles, req.msg.(*mgmtpb.PoolEvictReq).Handles...)
		}

		resp, err := svc.evictPoolConnections(ctx, batchReq)
		if err != nil {
			svc.log.Errorf("failed to evict pool connections for pool %s: %s", poolID, err)
			for _, req := range reqs {
				req.sendResponse(ctx, nil, err)
			}
			continue
		}
		for _, req := range reqs {
			req.sendResponse(ctx, resp, nil)
		}
	}

	return nil
}

// processBatchedMsgRequests processes a batch of requests for a given message type.
func (svc *mgmtSvc) processBatchedMsgRequests(ctx context.Context, bprChan batchProcessRespChan, msgName string, reqs []*batchRequest) {
	bpr := &batchProcessResp{msgName: msgName}
	if len(reqs) == 0 {
		select {
		case <-ctx.Done():
			return
		case bprChan <- bpr:
		}
		return
	}

	switch msgName {
	case string(proto.MessageName(new(mgmtpb.PoolEvictReq))):
		bpr.retryableReqs = svc.processBatchPoolEvictions(ctx, bprChan, reqs)
	default:
		svc.log.Errorf("no batch handler for message type %s", msgName)
	}

	select {
	case <-ctx.Done():
		return
	case bprChan <- bpr:
	}
}

// batchLoop is the main loop for processing batched requests.
func (svc *mgmtSvc) batchLoop(parent context.Context) {
	batchedMsgReqs := make(map[string][]*batchRequest)

	batchTimer := time.NewTicker(batchLoopInterval)
	defer batchTimer.Stop()

	svc.log.Debug("starting batchLoop")
	for {
		select {
		case <-parent.Done():
			svc.log.Debug("stopped batchLoop")
			return
		case req := <-svc.batchReqs:
			msgName := string(proto.MessageName(req.msg))
			if _, ok := batchedMsgReqs[msgName]; !ok {
				batchedMsgReqs[msgName] = []*batchRequest{}
			}
			batchedMsgReqs[msgName] = append(batchedMsgReqs[msgName], req)
		case <-batchTimer.C:
			batchedMsgNr := len(batchedMsgReqs)
			if batchedMsgNr == 0 {
				continue
			}

			bprChan := make(batchProcessRespChan, batchedMsgNr)
			for msgName, reqs := range batchedMsgReqs {
				svc.log.Debugf("processing %d %s requests", len(reqs), msgName)
				go svc.processBatchedMsgRequests(parent, bprChan, msgName, reqs)
			}

			for i := 0; i < batchedMsgNr; i++ {
				bpr := <-bprChan
				if len(bpr.retryableReqs) > 0 {
					batchedMsgReqs[bpr.msgName] = bpr.retryableReqs
				} else {
					delete(batchedMsgReqs, bpr.msgName)
				}
			}
		}
	}
}
