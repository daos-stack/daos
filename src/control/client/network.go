//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"bytes"
	"fmt"
	"io"
	"sort"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
)

const (
	networkScanTimeout = 10 * time.Second

	msgStreamRecv = "%T recv() failed"
	msgTypeAssert = "type assertion failed, wanted %T got %T"
)

type ProviderListResult struct {
	providerList string
}

type NetworkScanResult struct {
	provider string
	device   string
	numanode uint
	err      error
}

type NetworkScanResultMap map[string][]NetworkScanResult

func (nsrm NetworkScanResultMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(nsrm))

	for server := range nsrm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n%s\n", server, nsrm[server])
	}

	return buf.String()
}

func (nsr NetworkScanResult) String() string {
	var buf bytes.Buffer
	if nsr.err != nil {
		fmt.Fprintf(&buf, "\n\tError: %v\n", nsr.err)
		return buf.String()
	}
	fmt.Fprintf(&buf, "\n\tfabric_iface: %s\n\tprovider: %s\n\tpinned_numa_node: %d\n", nsr.device, nsr.provider, nsr.numanode)
	return buf.String()
}

func (c *connList) NetworkListProviders() ResultMap {
	c.log.Debugf("NetworkListProviders() Received")
	return c.makeRequests(&ctlpb.ProviderListRequest{}, providerListRequest)
}

func providerListRequest(mc Control, req interface{}, ch chan ClientResult) {
	sRes := ProviderListResult{}

	listReq, ok := req.(*ctlpb.ProviderListRequest)
	if !ok {
		err := errors.Errorf(msgTypeAssert, &ctlpb.ProviderListRequest{}, req)
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // type err
	}

	resp, err := mc.getCtlClient().NetworkListProviders(context.Background(), listReq)
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err} // return comms error
		return
	}
	mc.logger().Debugf("gRPC received: %s", resp.GetProvider())
	sRes.providerList = resp.GetProvider()
	ch <- ClientResult{mc.getAddress(), sRes.providerList, nil}
}

func (c *connList) NetworkScanDevices(searchProvider string) NetworkScanResultMap {
	c.log.Debugf("NetworkScanDevices() Received for provider: %s", searchProvider)
	cResults := c.makeRequests(searchProvider, networkScanRequest)
	cScanResults := make(NetworkScanResultMap)

	c.log.Debugf("\nNetworkScanDevices() Results:\n")
	for _, res := range cResults {
		if res.Err != nil {
			cScanResults[res.Address] = append(cScanResults[res.Address], NetworkScanResult{err: res.Err})
			continue
		}

		for _, results := range res.Value.([]NetworkScanResult) {
			cScanResults[res.Address] = append(cScanResults[res.Address], results)
		}
	}

	return cScanResults
}

func networkScanRequest(mc Control, parms interface{}, ch chan ClientResult) {
	var provider string
	sRes := []NetworkScanResult{}

	ctx, cancel := context.WithTimeout(context.Background(), networkScanTimeout)
	defer cancel()

	mc.logger().Debugf("parms has: %v", parms)

	switch parms.(type) {
	case string:
		provider = parms.(string)
	default:
		err := errors.Errorf(
			msgTypeAssert, ctlpb.DeviceScanRequest{}, parms)
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // type err
	}

	stream, err := mc.getCtlClient().NetworkScanDevices(ctx, &ctlpb.DeviceScanRequest{Provider: provider})

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
			ch <- ClientResult{mc.getAddress(), nil, err}
			return // recv err
		}

		provider := resp.GetProvider()
		device := resp.GetDevice()
		numanode := uint(resp.GetNumanode())
		mc.logger().Debugf("\nServer address: %s\n\tfabric_iface: %s\n\tprovider: %s\n\tpinned_numa_node: %d\n", mc.getAddress(), device, provider, numanode)
		sRes = append(sRes, NetworkScanResult{provider: provider, device: device, numanode: numanode})
	}
	ch <- ClientResult{mc.getAddress(), sRes, nil}
	mc.logger().Debugf("\nNormal exit networkScanRequest()\n")
}
