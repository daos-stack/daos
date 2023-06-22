//
// (C) Copyright 2018-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"reflect"
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

const (
	groupUpdateInterval = 500 * time.Millisecond
	batchLoopInterval   = 250 * time.Millisecond
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
		msgType       reflect.Type
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
	batchInterval     time.Duration
	batchReqs         batchReqChan
	serialReqs        batchReqChan
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
		batchInterval:     batchLoopInterval,
		batchReqs:         make(batchReqChan),
		serialReqs:        make(batchReqChan),
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
	if err := svc.checkSystemRequest(req); err != nil {
		return err
	}
	return svc.sysdb.CheckLeader()
}

// checkReplicaRequest performs sanity-checking on a request that must
// be run on a MS replica.
func (svc *mgmtSvc) checkReplicaRequest(req proto.Message) error {
	if err := svc.checkSystemRequest(req); err != nil {
		return err
	}
	return svc.sysdb.CheckReplica()
}

// startLeaderLoops kicks off the leader-only processing loops
// that will be canceled on leadership loss.
func (svc *mgmtSvc) startLeaderLoops(ctx context.Context) {
	go svc.leaderTaskLoop(ctx)
}

// startAsyncLoops kicks off the asynchronous processing loops.
func (svc *mgmtSvc) startAsyncLoops(ctx context.Context) {
	go svc.batchReqLoop(ctx)
	go svc.serialReqLoop(ctx)
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

// submitSerialRequest submits a message for serial processing and waits for a response.
func (svc *mgmtSvc) submitSerialRequest(ctx context.Context, msg proto.Message) (proto.Message, error) {
	respCh := make(batchRespChan, 1)
	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case svc.serialReqs <- &batchRequest{msg: msg, ctx: ctx, respCh: respCh}:
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
			req.sendResponse(ctx, nil, errors.Errorf("unexpected message type %T", req.msg))
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

// processBatchJoins processes a batch of JoinReq messages.
func (svc *mgmtSvc) processBatchJoins(ctx context.Context, bprChan batchProcessRespChan, reqs []*batchRequest) []*batchRequest {
	var updateNeeded bool
	for _, req := range reqs {
		msg, ok := req.msg.(*mgmtpb.JoinReq)
		if !ok {
			req.sendResponse(ctx, nil, errors.Errorf("unexpected message type %T", req.msg))
			continue
		}

		// Get the peer's address from the request context if it wasn't
		// specified in the request message.
		replyAddr, err := getPeerListenAddr(req.ctx, msg.Addr)
		if err != nil {
			req.sendResponse(ctx, nil, errors.Wrapf(err, "failed to parse %q into a peer control address", msg.Addr))
			continue
		}

		resp, err := svc.join(ctx, msg, replyAddr)
		req.sendResponse(ctx, resp, err)
		if err == nil {
			updateNeeded = true
		}
	}
	if updateNeeded {
		svc.log.Debug("requesting immediate group update after join(s)")
		svc.reqGroupUpdate(ctx, true)
	}

	return nil
}

// processBatchedMsgRequests processes a batch of requests for a given message type.
func (svc *mgmtSvc) processBatchedMsgRequests(ctx context.Context, bprChan batchProcessRespChan, msgType reflect.Type, reqs []*batchRequest) {
	bpr := &batchProcessResp{msgType: msgType}
	if len(reqs) == 0 {
		select {
		case <-ctx.Done():
			return
		case bprChan <- bpr:
		}
		return
	}

	switch reqs[0].msg.(type) {
	case *mgmtpb.PoolEvictReq:
		bpr.retryableReqs = svc.processBatchPoolEvictions(ctx, bprChan, reqs)
	case *mgmtpb.JoinReq:
		bpr.retryableReqs = svc.processBatchJoins(ctx, bprChan, reqs)
	default:
		svc.log.Errorf("no batch handler for message type %s", msgType)
	}

	select {
	case <-ctx.Done():
		return
	case bprChan <- bpr:
	}
}

// processSerialRequest processes a single serialized request.
func (svc *mgmtSvc) processSerialRequest(ctx context.Context, req *batchRequest) {
	svc.log.Debugf("invoking serial handler for %T", req.msg)
	switch msg := req.msg.(type) {
	case *mgmtpb.PoolCreateReq:
		resp, err := svc.poolCreate(ctx, msg)
		req.sendResponse(ctx, resp, err)
	default:
		svc.log.Errorf("no serial handler for message type %T", req.msg)
	}
}

// serialReqLoop is the main loop for processing serial requests.
func (svc *mgmtSvc) serialReqLoop(parent context.Context) {

	svc.log.Debug("starting serialReqLoop")
	for {
		select {
		case <-parent.Done():
			svc.log.Debug("stopped serialReqLoop")
			return
		case req := <-svc.serialReqs:
			svc.processSerialRequest(parent, req)
		}
	}
}

// batchReqLoop is the main loop for processing batched requests.
func (svc *mgmtSvc) batchReqLoop(parent context.Context) {
	batchedMsgReqs := make(map[reflect.Type][]*batchRequest)

	batchTimer := time.NewTicker(svc.batchInterval)
	defer batchTimer.Stop()

	svc.log.Debug("starting batchReqLoop")
	for {
		select {
		case <-parent.Done():
			svc.log.Debug("stopped batchReqLoop")
			return
		case req := <-svc.batchReqs:
			msgType := reflect.TypeOf(req.msg)
			if _, ok := batchedMsgReqs[msgType]; !ok {
				batchedMsgReqs[msgType] = []*batchRequest{}
			}
			batchedMsgReqs[msgType] = append(batchedMsgReqs[msgType], req)
		case <-batchTimer.C:
			batchedMsgNr := len(batchedMsgReqs)
			if batchedMsgNr == 0 {
				continue
			}

			bprChan := make(batchProcessRespChan, batchedMsgNr)
			for msgType, reqs := range batchedMsgReqs {
				svc.log.Debugf("processing %d %s requests", len(reqs), msgType)
				go svc.processBatchedMsgRequests(parent, bprChan, msgType, reqs)
			}

			for i := 0; i < batchedMsgNr; i++ {
				bpr := <-bprChan
				if len(bpr.retryableReqs) > 0 {
					batchedMsgReqs[bpr.msgType] = bpr.retryableReqs
				} else {
					delete(batchedMsgReqs, bpr.msgType)
				}
			}
		}
	}
}

// leaderTaskLoop is the main loop for handling MS leader tasks.
func (svc *mgmtSvc) leaderTaskLoop(parent context.Context) {
	var groupUpdateNeeded bool

	groupUpdateTimer := time.NewTicker(groupUpdateInterval)
	defer groupUpdateTimer.Stop()

	svc.log.Debug("starting leaderTaskLoop")
	for {
		select {
		case <-parent.Done():
			svc.log.Debug("stopped leaderTaskLoop")
			return
		case immediate := <-svc.groupUpdateReqs:
			groupUpdateNeeded = true
			if immediate {
				if err := svc.doGroupUpdate(parent, true); err != nil {
					svc.log.Errorf("immediate GroupUpdate failed: %s", err)
					continue
				}
				groupUpdateNeeded = false
			}
		case <-groupUpdateTimer.C:
			if !groupUpdateNeeded {
				continue
			}
			if err := svc.doGroupUpdate(parent, false); err != nil {
				svc.log.Errorf("lazy GroupUpdate failed: %s", err)
				continue
			}
			groupUpdateNeeded = false
		}
	}
}
