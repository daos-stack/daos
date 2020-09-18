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

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

type (
	// SmdDevice contains DAOS storage device information, including
	// health details if requested.
	SmdDevice struct {
		UUID      string                        `json:"uuid"`
		TargetIDs []int32                       `hash:"set" json:"tgt_ids"`
		State     string                        `json:"state"`
		Rank      system.Rank                   `json:"rank"`
		Health    *storage.NvmeControllerHealth `json:"health"`
	}

	// SmdPool contains the per-server components of a DAOS pool.
	SmdPool struct {
		UUID      string      `json:"uuid"`
		TargetIDs []int32     `hash:"set" json:"tgt_ids"`
		Blobs     []uint64    `hash:"set" json:"blobs"`
		Rank      system.Rank `hash:"set" json:"rank"`
	}

	// SmdPoolMap provides a map from pool UUIDs to per-rank pool info.
	SmdPoolMap map[string][]*SmdPool

	// SmdInfo encapsulates SMD-specific information.
	SmdInfo struct {
		Devices []*SmdDevice `hash:"set" json:"devices"`
		Pools   SmdPoolMap   `json:"pools"`
	}

	// SmdQueryReq contains the request parameters for a SMD query
	// operation.
	SmdQueryReq struct {
		unaryRequest
		OmitDevices      bool
		OmitPools        bool
		IncludeBioHealth bool
		SetFaulty        bool
		UUID             string // UUID of pool or device for single result
		Rank             system.Rank
		Target           string
	}

	// SmdQueryResp represents the results of performing
	// SMD query operations across a set of hosts.
	SmdQueryResp struct {
		HostErrorsResp
		HostStorage HostStorageMap `json:"host_storage_map"`
	}
)

func (si *SmdInfo) addRankPools(rank system.Rank, pools []*SmdPool) {
	for _, pool := range pools {
		if _, found := si.Pools[pool.UUID]; !found {
			si.Pools[pool.UUID] = make([]*SmdPool, 0, 1)
		}
		pool.Rank = rank
		si.Pools[pool.UUID] = append(si.Pools[pool.UUID], pool)
	}
}

func (sqr *SmdQueryResp) addHostResponse(hr *HostResponse) (err error) {
	pbResp, ok := hr.Message.(*mgmtpb.SmdQueryResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hs := &HostStorage{
		SmdInfo: &SmdInfo{
			Pools: make(SmdPoolMap),
		},
	}
	for _, rResp := range pbResp.GetRanks() {
		rank := system.Rank(rResp.Rank)

		rDevices := make([]*SmdDevice, len(rResp.GetDevices()))
		if err = convert.Types(rResp.GetDevices(), &rDevices); err != nil {
			return
		}
		for _, dev := range rDevices {
			dev.Rank = rank
			hs.SmdInfo.Devices = append(hs.SmdInfo.Devices, dev)
		}

		rPools := make([]*SmdPool, len(rResp.GetPools()))
		if err = convert.Types(rResp.GetPools(), &rPools); err != nil {
			return
		}
		hs.SmdInfo.addRankPools(rank, rPools)

	}

	if sqr.HostStorage == nil {
		sqr.HostStorage = make(HostStorageMap)
	}
	if err := sqr.HostStorage.Add(hr.Addr, hs); err != nil {
		return err
	}

	return
}

// SmdQuery concurrently performs per-server metadata operations across all
// hosts supplied in the request's hostlist, or all configured hosts if not
// explicitly specified. The function blocks until all results (successful
// or otherwise) are received, and returns a single response structure
// containing results for all SMD operations.
func SmdQuery(ctx context.Context, rpcClient UnaryInvoker, req *SmdQueryReq) (*SmdQueryResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.UUID != "" {
		if err := checkUUID(req.UUID); err != nil {
			return nil, err
		}
	}

	pbReq := new(mgmtpb.SmdQueryReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrap(err, "unable to convert request to protobuf")
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SmdQuery(ctx, pbReq)
	})

	if req.SetFaulty {
		reqHosts, err := getRequestHosts(DefaultConfig(), req)
		if err != nil {
			return nil, err
		}
		if len(reqHosts) > 1 {
			return nil, errors.New("cannot perform SetFaulty operation on > 1 host")
		}
	}

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	sqr := new(SmdQueryResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := sqr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := sqr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return sqr, nil
}
