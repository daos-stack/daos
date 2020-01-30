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

// RemoteHarnessReq provides request parameters to HarnessClient methods
type RemoteHarnessReq struct {
	Addr    string
	Ranks   []uint32
	Force   bool
	Timeout float32
}

// harnessClient implements the HarnessClient interface
type harnessClient struct {
	log          logging.Logger
	localHarness *IOServerHarness
	client       *mgmtSvcClient
}

// request is a wrapper that will operate over any provided requestFn.
//
// requestFn is a type alias for MgmtSvc fanout gRPCs that operate on a harness.
func (hc *harnessClient) prepareRequest(req RemoteHarnessReq) (*mgmtpb.RanksReq, error) {
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

func (hc *harnessClient) processResponse(rpcResp *mgmtpb.RanksResp) (system.MemberResults, error) {
	memberResults := make(system.MemberResults, 0, maxIoServers)
	if err := convert.Types(rpcResp.GetResults(), &memberResults); err != nil {
		return nil, err
	}

	return memberResults, nil
}

func (hc *harnessClient) Query(ctx context.Context, addr string, ranks ...uint32) (system.MemberResults, error) {
	hc.log.Debugf("issuing Query request to harness at %s", addr)

	rpcReq, err := hc.prepareRequest(RemoteHarnessReq{
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

	return hc.processResponse(rpcResp)
}

func (hc *harnessClient) PrepShutdown(ctx context.Context, addr string, ranks ...uint32) (system.MemberResults, error) {
	hc.log.Debugf("issuing PrepShutdown request to harness at %s", addr)

	rpcReq, err := hc.prepareRequest(RemoteHarnessReq{
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

	return hc.processResponse(rpcResp)
}

func (hc *harnessClient) Stop(ctx context.Context, addr string, force bool, ranks ...uint32) (system.MemberResults, error) {
	hc.log.Debugf("issuing Stop request to harness at %s", addr)

	rpcReq, err := hc.prepareRequest(RemoteHarnessReq{
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

	return hc.processResponse(rpcResp)
}

func (hc *harnessClient) Start(ctx context.Context, addr string, ranks ...uint32) (system.MemberResults, error) {
	hc.log.Debugf("issuing Start request to harness at %s", addr)

	rpcReq, err := hc.prepareRequest(RemoteHarnessReq{
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

	return hc.processResponse(rpcResp)
}

func NewHarnessClient(l logging.Logger, h *IOServerHarness) HarnessClient {
	return &harnessClient{log: l, localHarness: h}
}
