//
// (C) Copyright 2020-2022 Intel Corporation.
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

type netScanCmd struct {
	cmdutil.LogCmd
	cmdutil.JSONOutputCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider or 'all' for all available (default is all local providers)"`
}

func (cmd *netScanCmd) Execute(_ []string) error {
	var prov string
	if !strings.EqualFold(cmd.FabricProvider, "all") {
		prov = cmd.FabricProvider
	}

	fabricScanner := hwprov.DefaultFabricScanner(cmd.Logger)

	results, err := fabricScanner.Scan(context.Background(), prov)
	if err != nil {
		return nil
	}

	hf := fabricInterfaceSetToHostFabric(results, prov)
	hfm := make(control.HostFabricMap)
	if err := hfm.Add("localhost", hf); err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(hfm, nil)
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

		for _, devName := range netIFs.ToSlice() {
			for _, provider := range fi.Providers.ToSlice() {
				if filterProvider == "all" || strings.HasPrefix(provider.Name, filterProvider) {
					hf.AddInterface(&control.HostFabricInterface{
						Provider:    provider.Name,
						Device:      devName,
						NumaNode:    uint32(fi.NUMANode),
						NetDevClass: fi.DeviceClass,
						Priority:    uint32(provider.Priority),
					})
				}
			}
		}
	}

	return hf
}
