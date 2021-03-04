//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

const (
	defaultExcludeInterfaces = "lo"
)

type networkCmd struct {
	Scan networkScanCmd `command:"scan" description:"Scan for network interface devices on local server"`
}

// networkScanCmd is the struct representing the command to scan the machine for network interface devices
// that match the given fabric provider.
type networkScanCmd struct {
	cfgCmd
	logCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider or 'all' for all available (default is the provider specified in daos_server.yml)"`
}

func (cmd *networkScanCmd) Execute(args []string) error {
	if len(args) > 0 {
		return errors.WithMessage(nil, "failed to execute the fabric and device scan.  An invalid argument was provided.")
	}

	provider := cmd.config.Fabric.Provider
	switch {
	case strings.EqualFold(cmd.FabricProvider, "all"):
		provider = ""
	case cmd.FabricProvider != "":
		provider = cmd.FabricProvider
	}

	netCtx, err := netdetect.Init(context.Background())
	if err != nil {
		return err
	}
	defer netdetect.CleanUp(netCtx)

	results, err := netdetect.ScanFabric(netCtx, provider, defaultExcludeInterfaces)
	if err != nil {
		return errors.WithMessage(err, "failed to execute the fabric and device scan")
	}

	hf := &control.HostFabric{}
	for _, fi := range results {
		hf.AddInterface(&control.HostFabricInterface{
			Provider: fi.Provider,
			Device:   fi.DeviceName,
			NumaNode: uint32(fi.NUMANode),
		})
	}

	hfm := make(control.HostFabricMap)
	if err := hfm.Add("localhost", hf); err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintHostFabricMap(hfm, &bld); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return nil
}
