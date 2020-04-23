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

package main

import (
	"context"
	"os"
	"strings"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
)

// NetCmd is the struct representing the top-level network subcommand.
type NetCmd struct {
	Scan networkScanCmd `command:"scan" description:"Scan for network interface devices on remote servers"`
	List networkListCmd `command:"list" description:"List all known OFI providers on remote servers that are understood by 'scan'"`
}

// networkScanCmd is the struct representing the command to scan the machine for network interface devices
// that match the given fabric provider.
type networkScanCmd struct {
	logCmd
	ctlClientCmd
	jsonOutputCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider (default is the provider specified in daos_server.yml)"`
	AllProviders   bool   `short:"a" long:"all" description:"Specify 'all' to see all devices on all providers.  Overrides --provider"`
}

func (cmd *networkScanCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.NetworkScanReq{
		Provider: cmd.FabricProvider,
	}
	req.SetHostList(cmd.hostlist)

	cmd.log.Debugf("network scan req: %+v", req)

	resp, err := control.NetworkScan(ctx, cmd.ctlClient, req)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	var bld strings.Builder
	if err := control.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}
	if err := pretty.PrintHostFabricMap(resp.HostFabrics, &bld, false); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return resp.Errors()
}

type networkListCmd struct {
	logCmd
	ctlClientCmd
	jsonOutputCmd
}

// List the supported providers
func (cmd *networkListCmd) Execute(_ []string) error {
	ctx := context.Background()
	req := &control.NetworkScanReq{}
	req.SetHostList(cmd.hostlist)

	cmd.log.Debugf("network scan req: %+v", req)

	resp, err := control.NetworkScan(ctx, cmd.ctlClient, req)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, resp)
	}

	var bld strings.Builder
	if err := control.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}
	if err := pretty.PrintHostFabricMap(resp.HostFabrics, &bld, true); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return resp.Errors()
}
