//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"os"
	"strings"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

const (
	defaultExcludeInterfaces = "lo"
)

type netScanCmd struct {
	logCmd
	jsonOutputCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider or 'all' for all available (default is all local providers)"`
}

func (cmd *netScanCmd) printUnlessJson(fmtStr string, args ...interface{}) {
	if cmd.jsonOutputEnabled() {
		return
	}
	cmd.log.Infof(fmtStr, args...)
}

func (cmd *netScanCmd) Execute(_ []string) error {
	netCtx, err := netdetect.Init(context.Background())
	if err != nil {
		return err
	}
	defer netdetect.CleanUp(netCtx)

	if !netdetect.HasNUMA(netCtx) {
		cmd.printUnlessJson("This system is not NUMA aware.  Any devices found are reported as NUMA node 0.")
	}

	provider := cmd.FabricProvider
	if strings.EqualFold(cmd.FabricProvider, "all") {
		provider = ""
	}

	results, err := netdetect.ScanFabric(netCtx, provider, defaultExcludeInterfaces)
	if err != nil {
		exitWithError(cmd.log, err)
		return nil
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, results)
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
