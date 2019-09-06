//
// (C) Copyright 2018-2019 Intel Corporation.
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

package client

import (
	"fmt"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	types "github.com/daos-stack/daos/src/control/common/storage"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// BioHealthQuery will return all BIO device health and I/O error stats for
// given device UUID
func (c *connList) BioHealthQuery(req *pb.BioHealthReq) ResultQueryMap {
	results := make(ResultQueryMap)
	mc := c.controllers[0] // connect to first AP only for now

	resp, err := mc.getSvcClient().BioHealthQuery(context.Background(), req)

	result := ClientQueryResult{mc.getAddress(), resp, err}
	results[result.Address] = result

	return results
}

// SmdListDevs will list all devices in SMD device table
func (c *connList) SmdListDevs(req *pb.SmdDevReq) ResultSmdMap {
	results := make(ResultSmdMap)
	mc := c.controllers[0] // connect to first AP only for now

	resp, err := mc.getSvcClient().SmdListDevs(context.Background(), req)

	result := ClientSmdResult{mc.getAddress(), resp, err}
	results[result.Address] = result

	return results
}

// deviceHealthRequest returns all discovered NVMe SSDs and critical health
// statistics discovered on a remote server by calling over gRPC channel.
func deviceHealthRequest(mc Control, req interface{}, ch chan ClientResult) {
	sRes := StorageResult{}

	resp, err := mc.getCtlClient().DeviceHealthQuery(
		context.Background(), &pb.QueryHealthReq{})
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err} // return comms error
		return
	}

	// process storage subsystem responses
	nState := resp.GetNvmestate()
	if nState.GetStatus() != pb.ResponseStatus_CTRL_SUCCESS {
		msg := nState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("nvme %+v", nState.GetStatus())
		}
		sRes.nvmeCtrlr.Err = errors.Errorf(msg)
	} else {
		sRes.nvmeCtrlr.Ctrlrs = resp.Ctrlrs
	}

	ch <- ClientResult{mc.getAddress(), sRes, nil}
}

// DeviceHealthQuery returns health stats of NVMe SSDs attached to each
// remote server.
func (c *connList) DeviceHealthQuery() (ClientCtrlrMap) {
	cResults := c.makeRequests(nil, deviceHealthRequest)
	cCtrlrs := make(ClientCtrlrMap)   // mapping of server address to NVMe SSDs

	for _, res := range cResults {
		if res.Err != nil {
			cCtrlrs[res.Address] = types.CtrlrResults{Err: res.Err}
			continue
		}

		storageRes, ok := res.Value.(StorageResult)
		if !ok {
			err := fmt.Errorf(msgBadType, StorageResult{}, res.Value)

			cCtrlrs[res.Address] = types.CtrlrResults{Err: err}
			continue
		}

		cCtrlrs[res.Address] = storageRes.nvmeCtrlr
	}

	return cCtrlrs
}
