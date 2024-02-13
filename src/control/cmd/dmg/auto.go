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
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

type confGenRemoteFn func(ctx context.Context, req control.ConfGenerateRemoteReq) (*control.ConfGenerateRemoteResp, error)

// Package-local function pointer for backend API call. Enables mocking out package-external calls
// in unit tests.
var confGenRemoteCall confGenRemoteFn = control.ConfGenerateRemote

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
	cmdutil.ConfGenCmd
}

func (cmd *configGenCmd) confGen(ctx context.Context) (*config.Server, error) {
	cmd.Debugf("ConfGen called with command parameters %+v", cmd)

	// check cli then config for hostlist, default to localhost
	hl := cmd.getHostList()
	if len(hl) == 0 && cmd.config != nil {
		hl = cmd.config.HostList
	}
	if len(hl) == 0 {
		hl = []string{"localhost"}
	}

	req := control.ConfGenerateRemoteReq{
		ConfGenerateReq: control.ConfGenerateReq{},
		Client:          cmd.ctlInvoker,
		HostList:        hl,
	}
	if err := convert.Types(&cmd.ConfGenCmd, &req.ConfGenerateReq); err != nil {
		return nil, err
	}
	cmd.Debugf("control API ConfGenerateRemote called with req: %+v", req)

	// Use a modified commandline logger to send all log messages to stderr in debug mode
	// during the generation of server config file parameters so stdout can be reserved for
	// config file output only. If not in debug mode, only log >=error to stderr.
	logger := logging.NewCommandLineLogger()
	if cmd.Logger.EnabledFor(logging.LogLevelTrace) {
		cmd.Debug("debug mode detected, writing all logs to stderr")
		logger.ClearLevel(logging.LogLevelInfo)
		logger.WithInfoLogger(logging.NewCommandLineInfoLogger(os.Stderr))
	} else {
		// Suppress info logging.
		logger.SetLevel(logging.LogLevelError)
	}
	req.Log = logger

	resp, err := confGenRemoteCall(ctx, req)

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
	return cmd.confGenPrint(cmd.MustLogCtx())
}
