//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"strings"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
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
	resp.Interfaces = make([]*ctlpb.FabricInterface, len(results))
	if err := convert.Types(results, &resp.Interfaces); err != nil {
		return nil, errors.Wrap(err, "converting fabric interfaces to protobuf format")
	}

	resp.Numacount = int32(netdetect.NumNumaNodes(netCtx))
	resp.Corespernuma = int32(netdetect.CoresPerNuma(netCtx))

	c.log.Debugf("NetworkScanResp: %d NUMA nodes with %d cores each",
		resp.GetNumacount(), resp.GetCorespernuma())

	return resp, nil
}
