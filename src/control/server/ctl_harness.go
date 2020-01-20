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
	Query(context.Context, RemoteHarnessReq) (system.MemberResults, error)
	PrepShutdown(context.Context, RemoteHarnessReq) (system.MemberResults, error)
	Stop(context.Context, RemoteHarnessReq) (system.MemberResults, error)
	Start(context.Context, RemoteHarnessReq) (system.MemberResults, error)
}

// RemoteHarnessReq provides request parameters to HarnessClient methods
type RemoteHarnessReq struct {
	Addr    string
	Ranks   []uint32
	Timeout float32
	Force   bool
}

// NewRemoteHarnessReq returns pointer to RemoteHarnessReq.
func NewRemoteHarnessReq(action HarnessAction, addr string, ranks ...uint32) *RemoteHarnessReq {
	return &RemoteHarnessReq{Action: action, Addr: addr, Ranks: ranks}
}

// harnessClient implements the HarnessClient interface
type harnessClient struct {
	client *mgmtSvcClient
}

// requestFn is a type alias for MgmtSvc fanout gRPCs that operate on a harness.
type requestFn func(context.Context, string, *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error)

// request is a wrapper that will operate over any provided requestFn.
func (hc *harnessClient) request(ctx context.Context, req RemoteHarnessReq, fn requestFn) (system.MemberResults, error) {
	memberResults := make(system.MemberResults, 0, maxIoServers)

	if len(req.Ranks) > maxIoServers {
		return nil, errors.New("number of of ranks exceeds maximum")
	}

	rpcResp, err := fn(ctx, req.Addr,
		&mgmtpb.RanksReq{Ranks: req.Ranks, Timeout: req.Timeout, Force: req.Force})
	if err != nil {
		return nil, err
	}

	if err := convert.Types(rpcResp.GetResults(), &memberResults); err != nil {
		return nil, err
	}

	return memberResults, nil
}

func (hc *harnessClient) Query(ctx context.Context, req RemoteHarnessReq) (system.MemberResults, error) {
	if req.Force {
		return nil, errors.New("force request parameter not supported for Query")
	}
	return hc.request(ctx, req, hc.client.Status)
}

func (hc *harnessClient) PrepShutdown(ctx context.Context, req RemoteHarnessReq) (system.MemberResults, error) {
	if req.Force {
		return nil, errors.New("force request parameter not supported for PrepShutdown")
	}
	return hc.request(ctx, req, hc.client.PrepShutdown)
}

func (hc *harnessClient) Stop(ctx context.Context, req RemoteHarnessReq) (system.MemberResults, error) {
	return hc.request(ctx, req, hc.client.Stop)
}

func (hc *harnessClient) Start(ctx context.Context, req RemoteHarnessReq) (system.MemberResults, error) {
	if req.Force {
		return nil, errors.New("force request parameter not supported for Start")
	}
	return hc.request(ctx, req, hc.client.Start)
}
