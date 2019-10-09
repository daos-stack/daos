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
//	"bytes"
//	"fmt"
	"io"
//	"sort"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
)

type ProviderListResult struct {
	providerList	string
}

func (c *connList) GetProviderList() ResultMap {
	c.log.Debugf("RequestProviderList() Received")
	return c.makeRequests(&ctlpb.ProviderListRequest{}, providerListRequest)
}

func providerListRequest(mc Control, req interface{}, ch chan ClientResult) {
	sRes := ProviderListResult{}

	listReq, ok := req.(*ctlpb.ProviderListRequest)
	if !ok {
		err := errors.Errorf(msgTypeAssert, &ctlpb.ProviderListRequest{}, req)
		mc.logger().Errorf(err.Error())
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // type err
	}

	resp, err := mc.getCtlClient().RequestProviderList(context.Background(), listReq)
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err} // return comms error
		return
	}
	mc.logger().Debugf("gRPC received: %s", resp.GetProvider())
	sRes.providerList = resp.GetProvider()
	ch <- ClientResult{mc.getAddress(), sRes.providerList, nil}
}


type NetworkScanResult struct {
	provider	string
	device		string
	numanode	uint
}

func (c *connList) NetworkDeviceScanRequest(searchProvider string) ResultMap {
	c.log.Debugf("NetworkDeviceScanRequest() Received for provider: %s", searchProvider)
	cResults := c.makeRequests(&ctlpb.DeviceScanRequest{Provider: searchProvider}, networkScanRequest)
	cCtrlrResults := make(ResultMap)

	for _, res := range cResults {
		if res.Err != nil {
			//cCtrlrResults[res.Address] = res.Err
			continue
		}
/*
		storageRes, ok := res.Value.(StorageResult)
		if !ok {
			err := fmt.Errorf(msgBadType, StorageResult{}, res.Value)

			cCtrlrResults[res.Address] = types.CtrlrResults{Err: err}
			cMountResults[res.Address] = types.MountResults{Err: err}
			continue
		}
*/
		//cCtrlrResults[res.Address] = "My results from the network scan"
	}

	return cCtrlrResults
}

func networkScanRequest(mc Control, parms interface{}, ch chan ClientResult) {
	sRes := NetworkScanResult{}

	// TODO revisit this timeout
	ctx, cancel := context.WithTimeout(context.Background(), time.Minute)
	defer cancel()

	stream, err := mc.getCtlClient().RequestDeviceScan(ctx, &ctlpb.DeviceScanRequest{})
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // stream err
	}

	mc.logger().Debugf("networkScanRequest checking for responses")

	for {
		resp, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			err := errors.Wrapf(err, msgStreamRecv, stream)
			mc.logger().Errorf(err.Error())
			ch <- ClientResult{mc.getAddress(), nil, err}
			return // recv err
		}

		sRes.provider = resp.GetProvider()
		sRes.device = resp.GetDevice()
		sRes.numanode = uint(resp.GetNumanode())
		mc.logger().Debugf("scan results: %v", sRes)
		ch <- ClientResult{mc.getAddress(), sRes, nil}
	}
}
