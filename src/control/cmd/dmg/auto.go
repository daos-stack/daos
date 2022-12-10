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
	"github.com/daos-stack/daos/src/control/server/config"
)

// configCmd is the struct representing the top-level config subcommand.
type configCmd struct {
	Generate configGenCmd `command:"generate" alias:"gen" description:"Generate DAOS server configuration file based on discoverable hardware devices"`
}

type configGenCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd

	AccessPoints string `default:"localhost" short:"a" long:"access-points" description:"Comma separated list of access point addresses <ipv4addr/hostname>"`
	NrEngines    int    `short:"e" long:"num-engines" description:"Set the number of DAOS Engine sections to be populated in the config file output. If unset then the value will be set to the number of NUMA nodes on storage hosts in the DAOS system."`
	MinNrSSDs    int    `default:"1" short:"s" long:"min-ssds" description:"Minimum number of NVMe SSDs required per DAOS Engine (SSDs must reside on the host that is managing the engine). Set to 0 to generate a config with no NVMe."`
	NetClass     string `default:"best-available" short:"c" long:"net-class" description:"Network class preferred" choice:"best-available" choice:"ethernet" choice:"infiniband"`
}

func (cmd *configGenCmd) confGen(ctx context.Context) (*config.Server, error) {
	cmd.Debugf("ConfGen called with command parameters %+v", cmd)

	accessPoints := strings.Split(cmd.AccessPoints, ",")

	var ndc hardware.NetDevClass
	switch cmd.NetClass {
	case "ethernet":
		ndc = hardware.Ether
	case "infiniband":
		ndc = hardware.Infiniband
	case "best-available":
		ndc = hardware.NetDevAny
	default:
		return nil, errors.Errorf("unrecognized net-class value %s", cmd.NetClass)
	}

	req := control.ConfGenerateRemoteReq{
		ConfGenerateReq: control.ConfGenerateReq{
			Log:          cmd.Logger,
			NrEngines:    cmd.NrEngines,
			MinNrSSDs:    cmd.MinNrSSDs,
			NetClass:     ndc,
			AccessPoints: accessPoints,
		},
		Client: cmd.ctlInvoker,
	}
	if len(cmd.hostlist) == 0 || cmd.hostlist[0] == "" {
		cmd.hostlist = []string{"localhost"}
	}
	req.SetHostList(cmd.hostlist)

	// TODO: decide whether we want meaningful JSON output
	if cmd.jsonOutputEnabled() {
		return nil, cmd.outputJSON(nil, errors.New("JSON output not supported"))
	}

	cmd.Debugf("control API ConfGenerateRemote called with req: %+v", req)

	resp, err := control.ConfGenerateRemote(ctx, req)
	if err != nil {
		cge, ok := errors.Cause(err).(*control.ConfGenerateError)
		if !ok {
			// includes hardware validation errors e.g. hardware across hostset differs
			return nil, err
		}

		// host level errors e.g. unresponsive daos_server process
		var bld strings.Builder
		if err := pretty.PrintResponseErrors(cge, &bld); err != nil {
			return nil, err
		}
		cmd.Error(bld.String())
		return nil, err
	}

	cmd.Debugf("control API ConfGenerateRemote resp: %+v", resp)
	return &resp.Server, nil
}

// Execute is run when configGenCmd activates.
//
// Attempt to auto generate a server config file with populated storage and network hardware
// parameters suitable to be used across all hosts in provided host list. Use the control API to
// generate config from remote scan results.
func (cmd *configGenCmd) Execute(_ []string) error {
	ctx := context.Background()

	cfg, err := cmd.confGen(ctx)
	if err != nil {
		return err
	}

	bytes, err := yaml.Marshal(cfg)
	if err != nil {
		return err
	}

	// output recommended server config yaml file
	cmd.Info(string(bytes))
	return nil
}
