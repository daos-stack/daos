//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
)

type serverStarter func(logging.Logger, *config.Server) error

type startCmd struct {
	cmdutil.LogCmd
	cfgCmd
	start       serverStarter
	Port        uint16  `short:"p" long:"port" description:"Port for the gRPC management interfect to listen on"`
	MountPath   string  `short:"s" long:"storage" description:"Storage path"`
	Modules     *string `short:"m" long:"modules" description:"List of server modules to load"`
	Targets     uint16  `short:"t" long:"targets" description:"Number of targets to use (default use all cores)"`
	NrXsHelpers *uint16 `short:"x" long:"xshelpernr" description:"Number of helper XS per VOS target"`
	FirstCore   uint16  `short:"f" long:"firstcore" default:"0" description:"Index of first core for service thread"`
	Group       string  `short:"g" long:"group" description:"Server group name"`
	SocketDir   string  `short:"d" long:"socket_dir" description:"Location for all daos_server & daos_engine sockets"`
	Insecure    bool    `short:"i" long:"insecure" description:"Allow for insecure connections"`
	AutoFormat  bool    `long:"auto-format" description:"Automatically format storage on server start to bring-up engines without requiring dmg storage format command"`
}

func (cmd *startCmd) setCLIOverrides() error {
	// Override certificate support if specified in cliOpts
	if cmd.Insecure {
		cmd.config.TransportConfig.AllowInsecure = true
	}
	if cmd.Port > 0 {
		cmd.config.ControlPort = int(cmd.Port)
	}
	if cmd.MountPath != "" {
		cmd.Notice("-s, --storage is deprecated")
		if len(cmd.config.Engines) < 1 {
			return errors.New("config has zero engines")
		}
		if len(cmd.config.Engines[0].Storage.Tiers) < 1 ||
			!cmd.config.Engines[0].Storage.Tiers[0].IsSCM() {
			return errors.New("first storage tier of engine 0 must be SCM")
		}
		cmd.config.Engines[0].Storage.Tiers[0].Scm.MountPoint = cmd.MountPath
	}
	if cmd.Group != "" {
		cmd.config.WithSystemName(cmd.Group)
	}
	if cmd.SocketDir != "" {
		cmd.config.WithSocketDir(cmd.SocketDir)
	}
	if cmd.Modules != nil {
		cmd.config.WithModules(*cmd.Modules)
	}

	for _, srv := range cmd.config.Engines {
		if cmd.Targets > 0 {
			srv.WithTargetCount(int(cmd.Targets))
		}
		if cmd.NrXsHelpers != nil {
			srv.WithHelperStreamCount(int(*cmd.NrXsHelpers))
		}
		if cmd.FirstCore > 0 {
			srv.WithServiceThreadCore(int(cmd.FirstCore))
		}
	}

	return nil
}

func (cmd *startCmd) configureLogging() error {
	for i, srv := range cmd.config.Engines {
		if srv.LogFile == "" {
			cmd.Errorf("no daos log file specified for server %d", i)
		}
	}

	return cmdutil.ConfigureLogger(cmd.Logger, cmdutil.LogConfig{
		LogFile:  cmd.config.ControlLogFile,
		LogLevel: cmd.config.ControlLogMask,
		JSON:     cmd.config.ControlLogJSON,
	})
}

func (cmd *startCmd) Execute(args []string) error {
	if cmd.start == nil {
		cmd.start = server.Start
	}

	if err := cmd.configureLogging(); err != nil {
		return err
	}

	cmd.config.AutoFormat = cmd.AutoFormat

	return cmd.start(cmd.Logger, cmd.config)
}
