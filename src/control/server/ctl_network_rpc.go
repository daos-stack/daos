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
	providers, err := c.srvCfg.Fabric.GetProviders()
	if err != nil {
		return nil, err
	}

	switch {
	case strings.EqualFold(req.GetProvider(), "all"):
		providers = []string{}
	case req.GetProvider() != "":
		providers = []string{req.GetProvider()}
	}

	topo, err := hwprov.DefaultTopologyProvider(c.log).GetTopology(ctx)
	if err != nil {
		return nil, err
	}

	if err := c.fabric.CacheTopology(topo); err != nil {
		return nil, err
	}

	result, err := c.fabric.Scan(ctx, providers...)
	if err != nil {
		return nil, err
	}

	resp := c.fabricInterfaceSetToNetworkScanResp(result)

	resp.Numacount = int32(topo.NumNUMANodes())
	resp.Corespernuma = int32(topo.NumCoresPerNUMA())

	return resp, nil
}

func (c *ControlService) fabricInterfaceSetToNetworkScanResp(fis *hardware.FabricInterfaceSet) *ctlpb.NetworkScanResp {
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
				resp.Interfaces = append(resp.Interfaces, &ctlpb.FabricInterface{
					Provider:    prov.Name,
					Device:      hwFI,
					Numanode:    uint32(fi.NUMANode),
					Netdevclass: uint32(fi.DeviceClass),
					Priority:    uint32(prov.Priority),
				})
			}
		}
	}

	return resp
}
