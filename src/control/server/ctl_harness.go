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

type HarnessAction int

const (
	HarnessUnknown HarnessAction = iota
	HarnessQuery
	HarnessPrepShutdown
	HarnessStop
	HarnessStart

	defaultTimeout = 10 * time.Second
)

func (hs HarnessAction) String() string {
	return [...]string{
		"Unknown",
		"Query",
		"PrepShutdown",
		"Stop",
		"Start",
	}[hs]
}

type RemoteHarnessReq struct {
	Action HarnessAction
	Addr   string
	Ranks  []uint32
}

func NewRemoteHarnessReq(action HarnessAction, addr string, ranks ...uint32) *RemoteHarnessReq {
	return &RemoteHarnessReq{Action: action, Addr: addr, Ranks: ranks}
}

// harnessAction performs requested activity on a harness running on remote host.
//
// MgmtSvcClient request responses will contain results for each rank (system
// member) managed by a harness. Single RPC per harness rather than one per rank.
func harnessAction(ctx context.Context, msClient *mgmtSvcClient, req *RemoteHarnessReq) (system.MemberResults, error) {
	var requestFn func(context.Context, string, *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error)
	memberResults := make(system.MemberResults, 0, maxIoServers)
	timeout := defaultTimeout

	if len(req.Ranks) > maxIoServers {
		return nil, errors.New("number of of ranks exceeds maximum")
	}

	switch req.Action {
	case HarnessQuery:
		// TODO: implement msClient.Status to check responsiveness
		return nil, errors.New("HarnessQuery not implemented")
	case HarnessPrepShutdown:
		requestFn = msClient.PrepShutdown
	case HarnessStop:
		requestFn = msClient.Stop
		timeout = 5 * time.Second
	case HarnessStart:
		requestFn = msClient.Start
		timeout = 3 * time.Second
	default:
		return nil, errors.New("unknown harness action requested")
	}

	rpcResp, err := requestFn(ctx, req.Addr,
		&mgmtpb.RanksReq{Ranks: req.Ranks, Timeout: float32(timeout)})
	if err != nil {
		return nil, err
	}

	if err := convert.Types(rpcResp.GetResults(), &memberResults); err != nil {
		return nil, err
	}

	return memberResults, nil
}
