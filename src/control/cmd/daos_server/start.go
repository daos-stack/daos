//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
)

type serverStarter func(logging.Logger, *config.Server) error

type startCmd struct {
	cmdutil.LogCmd
	cfgCmd
	start               serverStarter
	Port                uint16  `short:"p" long:"port" description:"Port for the gRPC management interfect to listen on"`
	MountPath           string  `short:"s" long:"storage" description:"Storage path"`
	Modules             *string `short:"m" long:"modules" description:"List of server modules to load"`
	Targets             uint16  `short:"t" long:"targets" description:"number of targets to use (default use all cores)"`
	NrXsHelpers         *uint16 `short:"x" long:"xshelpernr" description:"number of helper XS per VOS target"`
	FirstCore           uint16  `short:"f" long:"firstcore" default:"0" description:"index of first core for service thread"`
	Group               string  `short:"g" long:"group" description:"Server group name"`
	SocketDir           string  `short:"d" long:"socket_dir" description:"Location for all daos_server & daos_engine sockets"`
	Insecure            bool    `short:"i" long:"insecure" description:"allow for insecure connections"`
	RecreateSuperblocks bool    `long:"recreate-superblocks" description:"recreate missing superblocks rather than failing"`
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
		cmd.Info("NOTICE: -s, --storage is deprecated")
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
	cmd.config.RecreateSuperblocks = cmd.RecreateSuperblocks

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
	log, ok := cmd.Logger.(*logging.LeveledLogger)
	if !ok {
		return errors.New("logger is not a LeveledLogger")
	}

	// Set log level mask for default logger from config,
	// unless it was explicitly set to debug via CLI flag.
	applyLogConfig := func() error {
		switch logging.LogLevel(cmd.config.ControlLogMask) {
		case logging.LogLevelDebug:
			log.SetLevel(logging.LogLevelDebug)
			cmd.Debugf("Switching control log level to DEBUG")
		case logging.LogLevelError:
			cmd.Debugf("Switching control log level to ERROR")
			log.SetLevel(logging.LogLevelError)
		}

		if cmd.config.ControlLogJSON {
			cmd.Logger = log.WithJSONOutput()
		}

		return nil
	}

	hostname, err := os.Hostname()
	if err != nil {
		return err
	}

	for i, srv := range cmd.config.Engines {
		if srv.LogFile == "" {
			cmd.Errorf("no daos log file specified for server %d", i)
		}
	}

	// Set log file for default logger if specified in config.
	if cmd.config.ControlLogFile != "" {
		f, err := common.AppendFile(cmd.config.ControlLogFile)
		if err != nil {
			return errors.WithMessage(err, "create log file")
		}

		cmd.Infof("%s logging to file %s",
			os.Args[0], cmd.config.ControlLogFile)

		// Create an additional set of loggers which append everything
		// to the specified file.
		cmd.Logger = log.
			WithErrorLogger(logging.NewErrorLogger(hostname, f)).
			WithInfoLogger(logging.NewInfoLogger(hostname, f)).
			WithDebugLogger(logging.NewDebugLogger(f))

		return applyLogConfig()
	}

	cmd.Info("no control log file specified; logging to stdout")

	return applyLogConfig()
}

func (cmd *startCmd) Execute(args []string) error {
	if cmd.start == nil {
		cmd.start = server.Start
	}

	if err := cmd.configureLogging(); err != nil {
		return err
	}

	return cmd.start(cmd.Logger, cmd.config)
}
