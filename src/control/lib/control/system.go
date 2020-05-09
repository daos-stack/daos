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

package control

import (
	"context"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/system"
)

// SystemStopReq contains the inputs for the system stop command.
type SystemStopReq struct {
	unaryRequest
	msRequest
	Prep  bool
	Kill  bool
	Ranks []system.Rank
	Force bool
}

// SystemStopResp contains the request response.
type SystemStopResp struct {
	Results system.MemberResults
}

// SystemStop will perform a controlled shutdown of DAOS system and a list
// of remaining system members on failure.
func SystemStop(ctx context.Context, rpcClient UnaryInvoker, req *SystemStopReq) (*SystemStopResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).SystemStop(ctx, &ctlpb.SystemStopReq{
			Prep:  req.Prep,
			Kill:  req.Kill,
			Force: req.Force,
			Ranks: system.RanksToUint32(req.Ranks),
		})
	})
	rpcClient.Debugf("DAOS system stop request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemStopResp)
	return resp, convertMSResponse(ur, resp)
}

// SystemStartReq contains the inputs for the system start request.
type SystemStartReq struct {
	unaryRequest
	msRequest
	Ranks []system.Rank
}

// SystemStartResp contains the request response.
type SystemStartResp struct {
	Results system.MemberResults // resulting from harness starts
}

// SystemStart will perform a start after a controlled shutdown of DAOS system.
func SystemStart(ctx context.Context, rpcClient UnaryInvoker, req *SystemStartReq) (*SystemStartResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).SystemStart(ctx, &ctlpb.SystemStartReq{
			Ranks: system.RanksToUint32(req.Ranks),
		})
	})
	rpcClient.Debugf("DAOS system start request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemStartResp)
	return resp, convertMSResponse(ur, resp)
}

// SystemQueryReq contains the inputs for the system query request.
type SystemQueryReq struct {
	unaryRequest
	msRequest
	Ranks []system.Rank
}

// SystemQueryResp contains the request response.
type SystemQueryResp struct {
	Members system.Members
}

// SystemQuery requests DAOS system status.
func SystemQuery(ctx context.Context, rpcClient UnaryInvoker, req *SystemQueryReq) (*SystemQueryResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewMgmtCtlClient(conn).SystemQuery(ctx, &ctlpb.SystemQueryReq{
			Ranks: system.RanksToUint32(req.Ranks),
		})
	})
	rpcClient.Debugf("DAOS system query request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemQueryResp)
	return resp, convertMSResponse(ur, resp)
}

// LeaderQueryReq contains the inputs for the leader query request.
type LeaderQueryReq struct {
	unaryRequest
	msRequest
	System string
}

// LeaderQueryResp contains the status of the request and, if successful, the
// MS leader and set of replicas in the system.
type LeaderQueryResp struct {
	Leader   string `json:"CurrentLeader"`
	Replicas []string
}

// LeaderQuery requests the current Management Service leader and the set of
// MS replicas.
func LeaderQuery(ctx context.Context, rpcClient UnaryInvoker, req *LeaderQueryReq) (*LeaderQueryResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).LeaderQuery(ctx, &mgmtpb.LeaderQueryReq{
			System: req.System,
		})
	})
	rpcClient.Debugf("DAOS system leader-query request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(LeaderQueryResp)
	return resp, convertMSResponse(ur, resp)
}

// ListPoolsReq contains the inputs for the list pools command.
type ListPoolsReq struct {
	unaryRequest
	msRequest
	System string
}

// ListPoolsResp contains the status of the request and, if successful, the list
// of pools in the system.
type ListPoolsResp struct {
	Status int32
	Pools  []*common.PoolDiscovery
}

// ListPools fetches the list of all pools and their service replicas from the
// system.
func ListPools(ctx context.Context, rpcClient UnaryInvoker, req *ListPoolsReq) (*ListPoolsResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).ListPools(ctx, &mgmtpb.ListPoolsReq{
			Sys: req.System,
		})
	})
	rpcClient.Debugf("DAOS system list-pools request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(ListPoolsResp)
	return resp, convertMSResponse(ur, resp)
}

// RanksReq contains the parameters for a system ranks request.
type RanksReq struct {
	unaryRequest
	Ranks []system.Rank
	Force bool
}

// RanksResp contains the response from a system ranks request.
type RanksResp struct {
	HostErrorsResp // record unresponsive hosts
	RankResults    system.MemberResults
}

// addHostResponse is responsible for validating the given HostResponse
// and adding it's results to the RanksResp.
func (srr *RanksResp) addHostResponse(hr *HostResponse) (err error) {
	pbResp, ok := hr.Message.(*mgmtpb.RanksResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	memberResults := make(system.MemberResults, 0)
	if err := convert.Types(pbResp.GetResults(), &memberResults); err != nil {
		if srr.HostErrors == nil {
			srr.HostErrors = make(HostErrorsMap)
		}
		return srr.HostErrors.Add(hr.Addr, err)
	}

	srr.RankResults = append(srr.RankResults, memberResults...)

	return
}

func rpcToRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	rr := new(RanksResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := rr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := rr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return rr, nil
}

// PrepShutdownRanks concurrently performs prep shutdown ranks across all hosts
// supplied in the request's hostlist. The function blocks until all
// results (successful or otherwise) are received, and returns a
// single response structure containing results for all host operations.
func PrepShutdownRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PrepShutdownRanks(ctx, &mgmtpb.RanksReq{
			Ranks: system.RanksToUint32(req.Ranks),
		})
	})
	rpcClient.Debugf("DAOS system prep shutdown-ranks request: %+v", req)

	return rpcToRanks(ctx, rpcClient, req)
}

// StopRanks concurrently performs stop ranks across all hosts
// supplied in the request's hostlist. The function blocks until all
// results (successful or otherwise) are received, and returns a
// single response structure containing results for all host operations.
func StopRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).StopRanks(ctx, &mgmtpb.RanksReq{
			Ranks: system.RanksToUint32(req.Ranks),
			Force: req.Force,
		})
	})
	rpcClient.Debugf("DAOS system stop-ranks request: %+v", req)

	return rpcToRanks(ctx, rpcClient, req)
}

// StartRanks concurrently performs start ranks across all hosts
// supplied in the request's hostlist. The function blocks until all
// results (successful or otherwise) are received, and returns a
// single response structure containing results for all host operations.
func StartRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).StartRanks(ctx, &mgmtpb.RanksReq{
			Ranks: system.RanksToUint32(req.Ranks),
		})
	})
	rpcClient.Debugf("DAOS system start-ranks request: %+v", req)

	return rpcToRanks(ctx, rpcClient, req)
}

// PingRanks concurrently performs ping on ranks across all hosts
// supplied in the request's hostlist. The function blocks until all
// results (successful or otherwise) are received, and returns a
// single response structure containing results for all host operations.
func PingRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PingRanks(ctx, &mgmtpb.RanksReq{
			Ranks: system.RanksToUint32(req.Ranks),
		})
	})
	rpcClient.Debugf("DAOS system ping-ranks request: %+v", req)

	return rpcToRanks(ctx, rpcClient, req)
}
