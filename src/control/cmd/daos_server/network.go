//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
)

type networkCmd struct {
	Scan networkScanCmd `command:"scan" description:"Scan for network interface devices on local server"`
}

// networkScanCmd is the struct representing the command to scan the machine for network interface devices
// that match the given fabric provider.
type networkScanCmd struct {
	cfgCmd
	cmdutil.LogCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider or 'all' for all available (default is the provider specified in daos_server.yml)"`
}

func (cmd *networkScanCmd) Execute(_ []string) error {
	fabricScanner := hwprov.DefaultFabricScanner(cmd.Logger)

	results, err := fabricScanner.Scan(context.Background())
	if err != nil {
		return nil
	}

	if cmd.FabricProvider == "" {
		prov, err := cmd.config.Fabric.GetPrimaryProvider()
		if err != nil {
			return err
		}

		cmd.FabricProvider = prov
	}

	hf := fabricInterfaceSetToHostFabric(results, cmd.FabricProvider)
	hfm := make(control.HostFabricMap)
	if err := hfm.Add("localhost", hf); err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintHostFabricMap(hfm, &bld); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return nil
}

func fabricInterfaceSetToHostFabric(fis *hardware.FabricInterfaceSet, filterProvider string) *control.HostFabric {
	hf := &control.HostFabric{}
	for _, fiName := range fis.Names() {
		fi, err := fis.GetInterface(fiName)
		if err != nil {
			continue
		}

		if fi.DeviceClass == hardware.Loopback {
			// Ignore loopback
			continue
		}

		netIFs := common.NewStringSet(fi.NetInterfaces.ToSlice()...)
		if len(fi.NetInterfaces) == 0 {
			netIFs.Add(fi.Name)
		}

		for _, name := range netIFs.ToSlice() {
			for _, provider := range fi.Providers.ToSlice() {
				if filterProvider == "all" || strings.HasPrefix(provider, filterProvider) {
					hf.AddInterface(&control.HostFabricInterface{
						Provider:    provider,
						Device:      name,
						NumaNode:    uint32(fi.NUMANode),
						NetDevClass: fi.DeviceClass,
					})
				}
			}
		}
	}

	return hf
}
