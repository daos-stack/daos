//
// (C) Copyright 2020 Intel Corporation.
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
	"strings"

	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

// configCmd is the struct representing the top-level config subcommand.
type configCmd struct {
	Generate configGenCmd `command:"generate" alias:"g" description:"Generate DAOS server configuration file based on discoverable hardware devices"`
}

type configGenCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	AccessPoints string `short:"a" long:"access-points" description:"Comma separated list of access point addresses <ipv4addr/hostname>"`
	NumPmem      int    `short:"p" long:"num-pmem" description:"Minimum number of SCM (pmem) devices required per storage host in DAOS system"`
	NumNvme      int    `short:"n" long:"num-nvme" description:"Minimum number of NVMe devices required per storage host in DAOS system"`
	NetClass     string `default:"best-available" short:"c" long:"net-class" description:"Network class preferred" choice:"best-available" choice:"ethernet" choice:"infiniband"`
}

// Execute is run when configGenCmd activates.
//
// Attempt to auto generate a server config file with populated storage and
// network hardware parameters suitable to be used on all hosts in provided host
// list.
func (cmd *configGenCmd) Execute(_ []string) error {
	ctx := context.Background()

	req := &control.ConfigGenerateReq{
		NumPmem:  cmd.NumPmem,
		NumNvme:  cmd.NumNvme,
		HostList: cmd.hostlist,
		Client:   cmd.ctlInvoker,
		Log:      cmd.log,
	}
	switch cmd.NetClass {
	case "ethernet":
		req.NetClass = netdetect.Ether
	case "infiniband":
		req.NetClass = netdetect.Infiniband
	default:
		req.NetClass = control.NetDevAny
	}
	if cmd.AccessPoints != "" {
		req.AccessPoints = strings.Split(cmd.AccessPoints, ",")
	}

	// TODO: decide whether we want meaningful JSON output
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(new(control.ConfigGenerateResp), nil)
	}

	resp, err := control.ConfigGenerate(ctx, req)

	// host level errors e.g. unresponsive daos_server process
	var bld strings.Builder
	if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
		return err
	}
	cmd.log.Info(bld.String()) // no-op if no host level errors

	// includes hardware validation errors e.g. hardware across hostset differs
	if err != nil {
		return err
	}

	bytes, err := yaml.Marshal(resp.ConfigOut)
	if err != nil {
		return err
	}

	// output recommended server config yaml file
	cmd.log.Info(string(bytes))
	return nil
}
