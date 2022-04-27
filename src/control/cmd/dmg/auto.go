//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
)

// configCmd is the struct representing the top-level config subcommand.
type configCmd struct {
	Generate configGenCmd `command:"generate" alias:"gen" description:"Generate DAOS server configuration file based on discoverable hardware devices"`
}

type configGenCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	AccessPoints string `short:"a" long:"access-points" description:"Comma separated list of access point addresses <ipv4addr/hostname>"`
	NrEngines    int    `short:"e" long:"num-engines" description:"Set the number of DAOS Engine sections to be populated in the config file output. If unset then the value will be set to the number of NUMA nodes on storage hosts in the DAOS system."`
	MinNrSSDs    int    `default:"1" short:"s" long:"min-ssds" description:"Minimum number of NVMe SSDs required per DAOS Engine (SSDs must reside on the host that is managing the engine). Set to 0 to generate a config with no NVMe."`
	NetClass     string `default:"best-available" short:"c" long:"net-class" description:"Network class preferred" choice:"best-available" choice:"ethernet" choice:"infiniband"`
}

// Execute is run when configGenCmd activates.
//
// Attempt to auto generate a server config file with populated storage and
// network hardware parameters suitable to be used on all hosts in provided host
// list.
func (cmd *configGenCmd) Execute(_ []string) error {
	ctx := context.Background()

	cmd.Debugf("configGenCmd input control config: %+v", cmd.config)

	req := control.ConfigGenerateReq{
		NrEngines: cmd.NrEngines,
		MinNrSSDs: cmd.MinNrSSDs,
		HostList:  cmd.config.HostList,
		Client:    cmd.ctlInvoker,
		Log:       cmd.Logger,
	}
	switch cmd.NetClass {
	case "ethernet":
		req.NetClass = hardware.Ether
	case "infiniband":
		req.NetClass = hardware.Infiniband
	default:
		req.NetClass = hardware.NetDevAny
	}
	if cmd.AccessPoints != "" {
		req.AccessPoints = strings.Split(cmd.AccessPoints, ",")
	}

	// TODO: decide whether we want meaningful JSON output
	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(nil, errors.New("JSON output not supported"))
	}

	resp, err := control.ConfigGenerate(ctx, req)
	if err != nil {
		cge, ok := errors.Cause(err).(*control.ConfigGenerateError)
		if !ok {
			// includes hardware validation errors e.g. hardware across hostset differs
			return err
		}

		// host level errors e.g. unresponsive daos_server process
		var bld strings.Builder
		if err := pretty.PrintResponseErrors(cge, &bld); err != nil {
			return err
		}
		cmd.Error(bld.String())
		return err
	}

	bytes, err := yaml.Marshal(resp.ConfigOut)
	if err != nil {
		return err
	}

	// output recommended server config yaml file
	cmd.Info(string(bytes))
	return nil
}
