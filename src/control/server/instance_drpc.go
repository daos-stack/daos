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

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/system"
)

func (srv *IOServerInstance) setDrpcClient(c drpc.DomainSocketClient) {
	srv.Lock()
	defer srv.Unlock()
	srv._drpcClient = c
}

func (srv *IOServerInstance) getDrpcClient() (drpc.DomainSocketClient, error) {
	srv.RLock()
	defer srv.RUnlock()
	if srv._drpcClient == nil {
		return nil, errors.New("no dRPC client set (data plane not started?)")
	}
	return srv._drpcClient, nil
}

// NotifyDrpcReady receives a ready message from the running IOServer
// instance.
func (srv *IOServerInstance) NotifyDrpcReady(msg *srvpb.NotifyReadyReq) {
	srv.log.Debugf("%s instance %d drpc ready: %v", DataPlaneName, srv.Index(), msg)

	// Activate the dRPC client connection to this iosrv
	srv.setDrpcClient(drpc.NewClientConnection(msg.DrpcListenerSock))

	go func() {
		srv.drpcReady <- msg
	}()
}

// awaitDrpcReady returns a channel which receives a ready message
// when the started IOServer instance indicates that it is
// ready to receive dRPC messages.
func (srv *IOServerInstance) awaitDrpcReady() chan *srvpb.NotifyReadyReq {
	return srv.drpcReady
}

// CallDrpc makes the supplied dRPC call via this instance's dRPC client.
func (srv *IOServerInstance) CallDrpc(module, method int32, body proto.Message) (*drpc.Response, error) {
	dc, err := srv.getDrpcClient()
	if err != nil {
		return nil, err
	}

	return makeDrpcCall(dc, module, method, body)
}

// drespToMemberResult converts drpc.Response to system.MemberResult.
//
// MemberResult is populated with rank, state and error dependent on processing
// dRPC response. Target state param is populated on success, Errored otherwise.
func drespToMemberResult(rank system.Rank, action string, dresp *drpc.Response, err error, tState system.MemberState) *system.MemberResult {
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

	return system.NewMemberResult(rank, action, outErr, state)
}

// dPing attempts dRPC request to given rank managed by instance and return
// success or error from call result or timeout encapsulated in result.
func (srv *IOServerInstance) dPing(ctx context.Context) *system.MemberResult {
	rank, err := srv.GetRank()
	if err != nil {
		return nil // no rank to return result for
	}

	if !srv.isReady() {
		return system.NewMemberResult(rank, "ping", nil, system.MemberStateStopped)
	}

	resChan := make(chan *system.MemberResult)
	go func() {
		dresp, err := srv.CallDrpc(drpc.ModuleMgmt, drpc.MethodPingRank, nil)
		resChan <- drespToMemberResult(rank, "ping", dresp, err, system.MemberStateReady)
	}()

	select {
	case <-ctx.Done():
		if ctx.Err() == context.DeadlineExceeded {
			return system.NewMemberResult(rank, "ping", ctx.Err(),
				system.MemberStateUnresponsive)
		}
		return nil // shutdown
	case result := <-resChan:
		return result
	}
}

// dPrepShutdown attempts dRPC request to given rank managed by instance and return
// success or error from call result or timeout encapsulated in result.
func (srv *IOServerInstance) dPrepShutdown(ctx context.Context) *system.MemberResult {
	rank, err := srv.GetRank()
	if err != nil {
		return nil // no rank to return result for
	}

	if !srv.isReady() {
		return system.NewMemberResult(rank, "prep shutdown", nil, system.MemberStateStopped)
	}

	resChan := make(chan *system.MemberResult)
	go func() {
		dresp, err := srv.CallDrpc(drpc.ModuleMgmt, drpc.MethodPrepShutdown, nil)
		resChan <- drespToMemberResult(rank, "prep shutdown", dresp, err,
			system.MemberStateStopping)
	}()

	select {
	case <-ctx.Done():
		if ctx.Err() == context.DeadlineExceeded {
			return system.NewMemberResult(rank, "prep shutdown", ctx.Err(),
				system.MemberStateUnresponsive)
		}
		return nil // shutdown
	case result := <-resChan:
		return result
	}
}
