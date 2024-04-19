//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"time"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	errDRPCNotReady   = errors.New("no dRPC client set (data plane not started?)")
	errEngineNotReady = errors.New("engine not ready yet")
)

func (ei *EngineInstance) setDrpcSocket(sock string) {
	ei.Lock()
	defer ei.Unlock()
	ei._drpcSocket = sock
}

func (ei *EngineInstance) getDrpcSocket() string {
	ei.RLock()
	defer ei.RUnlock()
	return ei._drpcSocket
}

func (ei *EngineInstance) getDrpcClient() drpc.DomainSocketClient {
	if ei.getDrpcClientFn == nil {
		ei.getDrpcClientFn = drpc.NewClientConnection
	}
	return ei.getDrpcClientFn(ei.getDrpcSocket())
}

// NotifyDrpcReady receives a ready message from the running Engine
// instance.
func (ei *EngineInstance) NotifyDrpcReady(msg *srvpb.NotifyReadyReq) {
	ei.log.Debugf("%s instance %d drpc ready: %v", build.DataPlaneName, ei.Index(), msg)

	ei.setDrpcSocket(msg.DrpcListenerSock)

	go func() {
		ei.drpcReady <- msg
	}()
}

// awaitDrpcReady returns a channel which receives a ready message
// when the started Engine instance indicates that it is
// ready to receive dRPC messages.
func (ei *EngineInstance) awaitDrpcReady() chan *srvpb.NotifyReadyReq {
	return ei.drpcReady
}

func (ei *EngineInstance) callDrpc(ctx context.Context, method drpc.Method, body proto.Message) (*drpc.Response, error) {
	dc := ei.getDrpcClient()

	rankMsg := ""
	if sb := ei.getSuperblock(); sb != nil && sb.Rank != nil {
		rankMsg = fmt.Sprintf(" (rank %s)", sb.Rank)
	}

	startedAt := time.Now()
	defer func() {
		ei.log.Debugf("dRPC to index %d%s: %s/%dB/%s", ei.Index(), rankMsg, method, proto.Size(body), time.Since(startedAt))
	}()

	return makeDrpcCall(ctx, ei.log, dc, method, body)
}

// CallDrpc makes the supplied dRPC call via this instance's dRPC client.
func (ei *EngineInstance) CallDrpc(ctx context.Context, method drpc.Method, body proto.Message) (*drpc.Response, error) {
	if !ei.IsStarted() {
		return nil, FaultDataPlaneNotStarted
	}
	if !ei.IsReady() {
		return nil, errEngineNotReady
	}

	return ei.callDrpc(ctx, method, body)
}

// drespToMemberResult converts drpc.Response to system.MemberResult.
//
// MemberResult is populated with rank, state and error dependent on processing
// dRPC response. Target state param is populated on success, Errored otherwise.
func drespToMemberResult(rank ranklist.Rank, dresp *drpc.Response, err error, tState system.MemberState) *system.MemberResult {
	if err != nil {
		return system.NewMemberResult(rank,
			errors.WithMessagef(err, "rank %s dRPC failed", &rank),
			system.MemberStateErrored)
	}

	resp := &mgmtpb.DaosResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return system.NewMemberResult(rank,
			errors.Errorf("rank %s dRPC unmarshal failed", &rank),
			system.MemberStateErrored)
	}
	if resp.GetStatus() != 0 {
		return system.NewMemberResult(rank,
			errors.Errorf("rank %s: %s", &rank, daos.Status(resp.GetStatus()).Error()),
			system.MemberStateErrored)
	}

	return system.NewMemberResult(rank, nil, tState)
}

// tryDrpc attempts dRPC request to given rank managed by instance and return
// success or error from call result or timeout encapsulated in result.
func (ei *EngineInstance) tryDrpc(ctx context.Context, method drpc.Method) *system.MemberResult {
	rank, err := ei.GetRank()
	if err != nil {
		return nil // no rank to return result for
	}

	localState := ei.LocalState()
	if localState != system.MemberStateReady {
		// member not ready for dRPC comms, annotate result with last error if stopped
		var err error
		if localState == system.MemberStateStopped && ei._lastErr != nil {
			err = ei._lastErr
		}
		return system.NewMemberResult(rank, err, localState)
	}

	// system member state that should be set on dRPC success
	targetState := system.MemberStateUnknown
	switch method {
	case drpc.MethodPrepShutdown:
		targetState = system.MemberStateStopping
	case drpc.MethodPingRank:
		targetState = system.MemberStateReady
	default:
		return system.NewMemberResult(rank,
			errors.Errorf("unsupported dRPC method (%s) for fanout", method),
			system.MemberStateErrored)
	}

	resChan := make(chan *system.MemberResult)
	go func(ctx context.Context) {
		dresp, err := ei.CallDrpc(ctx, method, nil)
		select {
		case <-ctx.Done():
		case resChan <- drespToMemberResult(rank, dresp, err, targetState):
		}
	}(ctx)

	select {
	case <-ctx.Done():
		if ctx.Err() == context.DeadlineExceeded {
			return system.NewMemberResult(rank, ctx.Err(), system.MemberStateUnresponsive)
		}
		return nil // shutdown
	case result := <-resChan:
		return result
	}
}

func getBioHealth(ctx context.Context, engine Engine, req *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error) {
	dresp, err := engine.CallDrpc(ctx, drpc.MethodBioHealth, req)
	if err != nil {
		return nil, errors.Wrap(err, "GetBioHealth dRPC call")
	}

	resp := &ctlpb.BioHealthResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal BioHealthQuery response")
	}

	if resp.Status != 0 {
		return nil, errors.Wrap(daos.Status(resp.Status), "GetBioHealth response status")
	}

	return resp, nil
}

func listSmdDevices(ctx context.Context, engine Engine, req *ctlpb.SmdDevReq) (*ctlpb.SmdDevResp, error) {
	dresp, err := engine.CallDrpc(ctx, drpc.MethodSmdDevs, req)
	if err != nil {
		return nil, err
	}

	resp := new(ctlpb.SmdDevResp)
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal SmdListDevs response")
	}

	if resp.Status != 0 {
		return nil, errors.Wrap(daos.Status(resp.Status), "ListSmdDevices failed")
	}

	return resp, nil
}
