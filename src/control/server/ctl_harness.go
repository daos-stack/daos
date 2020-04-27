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

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// HarnessClient provides methods for requesting actions on remote harnesses
// (attached to listening gRPC servers) identified by host addresses.
//
// Results will be provided for each rank (member) managed by harness.
// Single gRPC sent over management network per harness (not per rank).
type HarnessClient interface {
	Query(context.Context, string, ...system.Rank) (system.MemberResults, error)
	PrepShutdown(context.Context, string, ...system.Rank) (system.MemberResults, error)
	Stop(context.Context, string, bool, ...system.Rank) (system.MemberResults, error)
	Start(context.Context, string, ...system.Rank) (system.MemberResults, error)
}

// harnessClient implements the HarnessClient interface.
type harnessClient struct {
	log          logging.Logger
	localHarness *IOServerHarness
	client       *mgmtSvcClient
}

type harnessCall func(context.Context, string, mgmtpb.RanksReq) (*mgmtpb.RanksResp, error)

// prepareRequest will populate an MgmtSvcClient if missing and return RanksReq.
func (hc *harnessClient) prepareRequest(ranks []system.Rank, force bool) (*mgmtpb.RanksReq, error) {
	if hc.client == nil {
		mi, err := hc.localHarness.GetMSLeaderInstance()
		if err != nil {
			return nil, errors.Wrap(err, "prepare harness request")
		}
		hc.client = mi.msClient
	}

	if len(ranks) > maxIOServers {
		return nil, errors.New("number of of ranks exceeds maximum")
	}

	return &mgmtpb.RanksReq{
		Force: force, Ranks: system.RanksToUint32(ranks)}, nil
}

// call issues gRPC to remote harness using a supplied client function to the
// given address.
func (hc *harnessClient) call(ctx context.Context, addr string, rpcReq *mgmtpb.RanksReq, f harnessCall) (system.MemberResults, error) {
	errChan := make(chan error)
	var rpcResp *mgmtpb.RanksResp
	go func() {
		var innerErr error
		rpcResp, innerErr = f(ctx, addr, *rpcReq)

		select {
		case <-ctx.Done():
		case errChan <- innerErr:
		}
	}()

	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case err := <-errChan:
		if err != nil {
			return nil, err
		}
	}

	memberResults := make(system.MemberResults, 0, maxIOServers)
	if err := convert.Types(rpcResp.GetResults(), &memberResults); err != nil {
		return nil, errors.Wrapf(err, "decoding response from %s", addr)
	}

	return memberResults, nil
}

// Query sends Status gRPC using the MgmtSvcClient to the control server at the
// specified address to guage the responsiveness of the specified ranks.
//
// Results are returned for the ranks specified in the input parameter
// if they are being managed by the remote control server.
func (hc *harnessClient) Query(ctx context.Context, addr string, ranks ...system.Rank) (system.MemberResults, error) {
	rpcReq, err := hc.prepareRequest(ranks, false)
	if err != nil {
		return nil, err
	}

	return hc.call(ctx, addr, rpcReq, hc.client.Status)
}

// PrepShutdown sends PrepShutdown gRPC using the MgmtSvcClient to the control
// server at the specified address to prepare the specified ranks for a
// controlled shutdown.
//
// Results are returned for the ranks specified in the input parameter
// if they are being managed by the remote control server.
func (hc *harnessClient) PrepShutdown(ctx context.Context, addr string, ranks ...system.Rank) (system.MemberResults, error) {
	rpcReq, err := hc.prepareRequest(ranks, false)
	if err != nil {
		return nil, err
	}

	return hc.call(ctx, addr, rpcReq, hc.client.PrepShutdown)
}

// Stop sends Stop gRPC using the MgmtSvcClient to the control server at the
// specified address to terminate the specified ranks.
//
// Results are returned for the ranks specified in the input parameter
// if they are being managed by the remote control server.
//
// Ranks will be forcefully stopped if the force parameter is specified.
func (hc *harnessClient) Stop(ctx context.Context, addr string, force bool, ranks ...system.Rank) (system.MemberResults, error) {
	rpcReq, err := hc.prepareRequest(ranks, force)
	if err != nil {
		return nil, err
	}

	return hc.call(ctx, addr, rpcReq, hc.client.Stop)
}

// Start sends Start gRPC using the MgmtSvcClient to the control server at the
// specified address to start the specified ranks.
//
// Results are returned for the ranks specified in the input parameter
// if they are being managed by the remote control server.
func (hc *harnessClient) Start(ctx context.Context, addr string, ranks ...system.Rank) (system.MemberResults, error) {
	rpcReq, err := hc.prepareRequest(ranks, false)
	if err != nil {
		return nil, err
	}

	return hc.call(ctx, addr, rpcReq, hc.client.Start)
}

// NewHarnessClient returns a new harnessClient reference containing a reference
// to the harness on the locally running control server.
func NewHarnessClient(l logging.Logger, h *IOServerHarness) HarnessClient {
	return &harnessClient{log: l, localHarness: h}
}
