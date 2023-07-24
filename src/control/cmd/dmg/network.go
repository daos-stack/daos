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
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
)

// NetCmd is the struct representing the top-level network subcommand.
type NetCmd struct {
	Scan networkScanCmd `command:"scan" description:"Scan for network interface devices on remote servers"`
}

// networkScanCmd is the struct representing the command to scan the machine for network interface devices
// that match the given fabric provider.
type networkScanCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider or 'all' for all available (default is the provider specified in daos_server.yml)"`
}

func (cmd *networkScanCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.NetworkScanReq{
		Provider: cmd.FabricProvider,
	}

	req.SetHostList(cmd.getHostList())

	cmd.Debugf("network scan req: %+v", req)

	resp, err := control.NetworkScan(ctx, cmd.ctlInvoker, req)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	if err != nil {
		return err
	}

	var bld strings.Builder
	if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}

	if err := pretty.PrintHostFabricMap(resp.HostFabrics, &bld); err != nil {
		return err
	}
	cmd.Info(bld.String())

	return resp.Errors()
}
