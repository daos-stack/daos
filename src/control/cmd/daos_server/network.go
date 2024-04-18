//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/server/config"
)

const allProviders = "all"

type (
	fabricScanFn     func(context.Context, ...string) (*hardware.FabricInterfaceSet, error)
	initNetworkCmdFn func(cmd *networkScanCmd) (fabricScanFn, *config.Server, error)
)

func initNetworkCmd(cmd *networkScanCmd) (fabricScanFn, *config.Server, error) {
	if err := common.CheckDupeProcess(); err != nil {
		return nil, nil, err
	}

	return hwprov.DefaultFabricScanner(cmd.Logger).Scan, cmd.config, nil
}

type networkCmd struct {
	Scan networkScanCmd `command:"scan" description:"Scan for network interface devices on local server"`
}

// networkScanCmd is the struct representing the command to scan the machine for network interface devices
// that match the given fabric provider.
type networkScanCmd struct {
	baseScanCmd
	scan           fabricScanFn
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider or 'all' for all available (default is the provider specified in daos_server.yml)"`
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
				if filterProvider == allProviders ||
					strings.HasPrefix(provider.Name, filterProvider) {
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

func GetLocalFabricIfaces(ctx context.Context, scan fabricScanFn, filterProvider string) (*control.HostFabric, error) {
	results, err := scan(ctx, filterProvider)
	if err != nil {
		return nil, err
	}

	return fabricInterfaceSetToHostFabric(results, filterProvider), nil
}

func localHostFabricMap(hf *control.HostFabric) (control.HostFabricMap, error) {
	hfm := make(control.HostFabricMap)

	return hfm, hfm.Add("localhost", hf)
}

func (cmd *networkScanCmd) initWith(initFn initNetworkCmdFn) error {
	var err error
	cmd.scan, cmd.config, err = initFn(cmd)
	if err != nil {
		return err
	}

	return nil
}

func (cmd *networkScanCmd) Execute(_ []string) error {
	ctx := cmd.MustLogCtx()

	var prov string
	switch {
	case cmd.FabricProvider != "":
		if !strings.EqualFold(cmd.FabricProvider, allProviders) {
			prov = cmd.FabricProvider
		}
	case cmd.config != nil && cmd.config.Fabric.Provider != "":
		priProv, err := cmd.config.Fabric.GetPrimaryProvider()
		if err != nil {
			return errors.Wrapf(err, "unable to get fabric provider from config")
		}
		prov = priProv
	}

	hf, err := GetLocalFabricIfaces(ctx, cmd.scan, prov)
	if err != nil {
		return errors.Wrap(err, "get local fabric interfaces")
	}
	cmd.Debugf("discovered fabric interfaces: %+v", hf.Interfaces)

	hfm, err := localHostFabricMap(hf)
	if err != nil {
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
