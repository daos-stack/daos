//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"strings"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

const (
	defaultExcludeInterfaces = "lo"
)

// NetworkScan retrieves details of network interfaces on remote hosts.
func (c *ControlService) NetworkScan(ctx context.Context, req *ctlpb.NetworkScanReq) (*ctlpb.NetworkScanResp, error) {
	c.log.Debugf("NetworkScanDevices() Received request: %s", req.GetProvider())
	excludes := req.GetExcludeinterfaces()
	if excludes == "" {
		excludes = defaultExcludeInterfaces
	}

	provider := c.srvCfg.Fabric.Provider
	switch {
	case strings.EqualFold(req.GetProvider(), "all"):
		provider = ""
	case req.GetProvider() != "":
		provider = req.GetProvider()
	}

	netCtx, err := netdetect.Init(context.Background())
	if err != nil {
		return nil, err
	}
	defer netdetect.CleanUp(netCtx)

	results, err := netdetect.ScanFabric(netCtx, provider, excludes)
	if err != nil {
		return nil, errors.WithMessage(err, "failed to execute the fabric and device scan")
	}

	resp := new(ctlpb.NetworkScanResp)
	for _, sr := range results {
		resp.Interfaces = append(resp.Interfaces, &ctlpb.FabricInterface{
			Provider:    sr.Provider,
			Device:      sr.DeviceName,
			Numanode:    uint32(sr.NUMANode),
			Priority:    uint32(sr.Priority),
			Netdevclass: sr.NetDevClass,
		})
	}

	resp.Numacount = int32(netdetect.NumNumaNodes(netCtx))

	return resp, nil
}
