//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"strings"

	"golang.org/x/net/context"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
)

// NetworkScan retrieves details of network interfaces on remote hosts.
func (c *ControlService) NetworkScan(ctx context.Context, req *ctlpb.NetworkScanReq) (*ctlpb.NetworkScanResp, error) {
	c.log.Debugf("NetworkScanDevices() Received request: %s", req.GetProvider())

	provider := c.srvCfg.Fabric.Provider
	switch {
	case strings.EqualFold(req.GetProvider(), "all"):
		provider = ""
	case req.GetProvider() != "":
		provider = req.GetProvider()
	}

	topo, err := hwprov.DefaultTopologyProvider(c.log).GetTopology(ctx)
	if err != nil {
		return nil, err
	}

	if err := c.fabric.CacheTopology(topo); err != nil {
		return nil, err
	}

	result, err := c.fabric.Scan(ctx)
	if err != nil {
		return nil, err
	}

	resp := c.fabricInterfaceSetToNetworkScanResp(result, provider)

	resp.Numacount = int32(topo.NumNUMANodes())
	resp.Corespernuma = int32(topo.NumCoresPerNUMA())

	c.log.Debugf("NetworkScanResp: %d NUMA nodes with %d cores each",
		resp.GetNumacount(), resp.GetCorespernuma())

	return resp, nil
}

func (c *ControlService) fabricInterfaceSetToNetworkScanResp(fis *hardware.FabricInterfaceSet, provider string) *ctlpb.NetworkScanResp {
	resp := new(ctlpb.NetworkScanResp)
	resp.Interfaces = make([]*ctlpb.FabricInterface, 0, fis.NumNetDevices())
	for _, name := range fis.Names() {
		fi, err := fis.GetInterface(name)
		if err != nil {
			c.log.Errorf("unexpected error getting IF %q: %s", name, err.Error())
			continue
		}

		if fi.DeviceClass == hardware.Loopback {
			continue
		}

		for _, hwFI := range fi.NetInterfaces.ToSlice() {
			for _, prov := range fi.Providers.ToSlice() {
				if provider == "" || provider == prov {
					resp.Interfaces = append(resp.Interfaces, &ctlpb.FabricInterface{
						Provider:    prov,
						Device:      hwFI,
						Numanode:    uint32(fi.NUMANode),
						Netdevclass: uint32(fi.DeviceClass),
					})
				}
			}
		}
	}

	return resp
}
