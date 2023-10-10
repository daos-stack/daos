//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"os"
	"strings"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

var ErrTmpfsNoExtMDPath = errors.New("--use-tmpfs-scm will generate an md-on-ssd config and so " +
	"--control-metadata-path must also be set")

// configCmd is the struct representing the top-level config subcommand.
type configCmd struct {
	Generate configGenCmd `command:"generate" alias:"gen" description:"Generate DAOS server configuration file based on discoverable hardware devices"`
}

type configGenCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd

	AccessPoints    string `default:"localhost" short:"a" long:"access-points" description:"Comma separated list of access point addresses <ipv4addr/hostname>"`
	NrEngines       int    `short:"e" long:"num-engines" description:"Set the number of DAOS Engine sections to be populated in the config file output. If unset then the value will be set to the number of NUMA nodes on storage hosts in the DAOS system."`
	SCMOnly         bool   `short:"s" long:"scm-only" description:"Create a SCM-only config without NVMe SSDs."`
	NetClass        string `default:"infiniband" short:"c" long:"net-class" description:"Set the network class to be used" choice:"ethernet" choice:"infiniband"`
	NetProvider     string `short:"p" long:"net-provider" description:"Set the network provider to be used"`
	UseTmpfsSCM     bool   `short:"t" long:"use-tmpfs-scm" description:"Use tmpfs for scm rather than PMem"`
	ExtMetadataPath string `short:"m" long:"control-metadata-path" description:"External storage path to store control metadata in MD-on-SSD mode"`
}

func (cmd *configGenCmd) confGen(ctx context.Context) (*config.Server, error) {
	cmd.Debugf("ConfGen called with command parameters %+v", cmd)

	if cmd.UseTmpfsSCM && cmd.ExtMetadataPath == "" {
		return nil, ErrTmpfsNoExtMDPath
	}

	accessPoints := strings.Split(cmd.AccessPoints, ",")

	var ndc hardware.NetDevClass
	switch cmd.NetClass {
	case "ethernet":
		ndc = hardware.Ether
	case "infiniband":
		ndc = hardware.Infiniband
	default:
		return nil, errors.Errorf("unrecognized net-class value %s", cmd.NetClass)
	}

	// check cli then config for hostlist, default to localhost
	hl := cmd.getHostList()
	if len(hl) == 0 && cmd.config != nil {
		hl = cmd.config.HostList
	}
	if len(hl) == 0 {
		hl = []string{"localhost"}
	}

	req := control.ConfGenerateRemoteReq{
		ConfGenerateReq: control.ConfGenerateReq{
			Log:             cmd.Logger,
			NrEngines:       cmd.NrEngines,
			SCMOnly:         cmd.SCMOnly,
			NetClass:        ndc,
			NetProvider:     cmd.NetProvider,
			AccessPoints:    accessPoints,
			UseTmpfsSCM:     cmd.UseTmpfsSCM,
			ExtMetadataPath: cmd.ExtMetadataPath,
		},
		Client:   cmd.ctlInvoker,
		HostList: hl,
	}
	cmd.Debugf("control API ConfGenerateRemote called with req: %+v", req)

	// Use a modified commandline logger to send all log messages to stderr during the
	// generation of server config file parameters so stdout can be reserved for config file
	// output only.
	logStderr := logging.NewCommandLineLogger()
	logStderr.ClearLevel(logging.LogLevelInfo)
	logStderr.WithInfoLogger(logging.NewCommandLineInfoLogger(os.Stderr))
	req.Log = logStderr

	resp, err := control.ConfGenerateRemote(ctx, req)

	if cmd.JSONOutputEnabled() {
		return nil, cmd.OutputJSON(resp, err)
	}

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

func (cmd *configGenCmd) confGenPrint(ctx context.Context) error {
	cfg, err := cmd.confGen(ctx)
	if cmd.JSONOutputEnabled() || err != nil {
		return err
	}

	bytes, err := yaml.Marshal(cfg)
	if err != nil {
		return err
	}

	// Print generated config yaml file contents to stdout.
	cmd.Info(string(bytes))
	return nil
}

// Execute is run when configGenCmd activates.
//
// Attempt to auto generate a server config file with populated storage and network hardware
// parameters suitable to be used across all hosts in provided host list. Use the control API to
// generate config from remote scan results.
func (cmd *configGenCmd) Execute(_ []string) error {
	return cmd.confGenPrint(context.Background())
}
