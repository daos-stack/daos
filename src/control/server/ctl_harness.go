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
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// defaultTimeout used in harness requests.
const defaultTimeout = 10 * time.Second

// HarnessClient provides methods for requesting actions on remote harnesses
// (attached to listening gRPC servers) identified by host addresses.
//
// Results will be provided for each rank (member) managed by harness.
// Single gRPC sent over management network per harness (not per rank).
type HarnessClient interface {
	Query(context.Context, string, ...uint32) (system.MemberResults, error)
	PrepShutdown(context.Context, string, ...uint32) (system.MemberResults, error)
	Stop(context.Context, string, bool, ...uint32) (system.MemberResults, error)
	Start(context.Context, string, ...uint32) (system.MemberResults, error)
}

// RemoteHarnessReq provides request parameters to HarnessClient methods.
type RemoteHarnessReq struct {
	Addr    string
	Ranks   []uint32
	Force   bool
	Timeout float32
}

// harnessClient implements the HarnessClient interface.
type harnessClient struct {
	log          logging.Logger
	localHarness *IOServerHarness
	client       *mgmtSvcClient
}

// prepareRPC will make sure we have a MgmtSvcClient and return a populated
// RanksReq proto message from a provided RemoteHarnessReq.
func (hc *harnessClient) prepareRPC(req RemoteHarnessReq) (*mgmtpb.RanksReq, error) {
	// Populate MS instance to use as MgmtSvcClient
	if hc.client == nil {
		mi, err := hc.localHarness.GetMSLeaderInstance()
		if err != nil {
			return nil, errors.Wrap(err, "HarnessClient request")
		}
		hc.client = mi.msClient
	}

	if len(req.Ranks) > maxIoServers {
		return nil, errors.New("number of of ranks exceeds maximum")
	}

	return &mgmtpb.RanksReq{Ranks: req.Ranks, Timeout: req.Timeout, Force: req.Force}, nil
}

// processRPC takes a RanksResp proto message and converts to system.MemberResults.
func (hc *harnessClient) processRPC(rpcResp *mgmtpb.RanksResp) (system.MemberResults, error) {
	memberResults := make(system.MemberResults, 0, maxIoServers)
	if err := convert.Types(rpcResp.GetResults(), &memberResults); err != nil {
		return nil, err
	}

	return memberResults, nil
}

// Query sends Status gRPC using the MgmtSvcClient to the control server at the
// specified address to guage the responsiveness of the specified ranks.
//
// Results are returned for the ranks specified in the input parameter
// if they are being managed by the remote control server.
func (hc *harnessClient) Query(ctx context.Context, addr string, ranks ...uint32) (system.MemberResults, error) {
	hc.log.Debugf("issuing Query request to harness at %s", addr)

	rpcReq, err := hc.prepareRPC(RemoteHarnessReq{
		Addr: addr, Ranks: ranks, Force: false, Timeout: float32(defaultTimeout),
	})
	if err != nil {
		return nil, err
	}

	var rpcResp *mgmtpb.RanksResp
	rpcResp, err = hc.client.Status(ctx, addr, *rpcReq)
	if err != nil {
		return nil, err
	}

	return hc.processRPC(rpcResp)
}

// PrepShutdown sends PrepShutdown gRPC using the MgmtSvcClient to the control
// server at the specified address to prepare the specified ranks for a
// controlled shutdown.
//
// Results are returned for the ranks specified in the input parameter
// if they are being managed by the remote control server.
func (hc *harnessClient) PrepShutdown(ctx context.Context, addr string, ranks ...uint32) (system.MemberResults, error) {
	hc.log.Debugf("issuing PrepShutdown request to harness at %s", addr)

	rpcReq, err := hc.prepareRPC(RemoteHarnessReq{
		Addr: addr, Ranks: ranks, Force: false, Timeout: float32(defaultTimeout),
	})
	if err != nil {
		return nil, err
	}

	var rpcResp *mgmtpb.RanksResp
	rpcResp, err = hc.client.PrepShutdown(ctx, addr, *rpcReq)
	if err != nil {
		return nil, err
	}

	return hc.processRPC(rpcResp)
}

// Stop sends Stop gRPC using the MgmtSvcClient to the control server at the
// specified address to terminate the specified ranks.
//
// Results are returned for the ranks specified in the input parameter
// if they are being managed by the remote control server.
//
// Ranks will be forcefully stopped if the force parameter is specified.
func (hc *harnessClient) Stop(ctx context.Context, addr string, force bool, ranks ...uint32) (system.MemberResults, error) {
	hc.log.Debugf("issuing Stop request to harness at %s", addr)

	rpcReq, err := hc.prepareRPC(RemoteHarnessReq{
		Addr: addr, Ranks: ranks, Force: force, Timeout: float32(defaultTimeout),
	})
	if err != nil {
		return nil, err
	}

	var rpcResp *mgmtpb.RanksResp
	rpcResp, err = hc.client.Stop(ctx, addr, *rpcReq)
	if err != nil {
		return nil, err
	}

	return hc.processRPC(rpcResp)
}

// Start sends Stop gRPC using the MgmtSvcClient to the control server at the
// specified address to start the specified ranks.
//
// Results are returned for the ranks specified in the input parameter
// if they are being managed by the remote control server.
func (hc *harnessClient) Start(ctx context.Context, addr string, ranks ...uint32) (system.MemberResults, error) {
	hc.log.Debugf("issuing Start request to harness at %s", addr)

	rpcReq, err := hc.prepareRPC(RemoteHarnessReq{
		Addr: addr, Ranks: ranks, Force: false, Timeout: float32(defaultTimeout),
	})
	if err != nil {
		return nil, err
	}

	var rpcResp *mgmtpb.RanksResp
	rpcResp, err = hc.client.Start(ctx, addr, *rpcReq)
	if err != nil {
		return nil, err
	}

	return hc.processRPC(rpcResp)
}

// NewHarnessClient returns a new harnessClient reference containing a reference
// to the harness on the locally running control server.
func NewHarnessClient(l logging.Logger, h *IOServerHarness) HarnessClient {
	return &harnessClient{log: l, localHarness: h}
}
