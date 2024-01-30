//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
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
	Targets             uint16  `short:"t" long:"targets" description:"Number of targets to use (default use all cores)"`
	NrXsHelpers         *uint16 `short:"x" long:"xshelpernr" description:"Number of helper XS per VOS target"`
	FirstCore           uint16  `short:"f" long:"firstcore" default:"0" description:"Index of first core for service thread"`
	Group               string  `short:"g" long:"group" description:"Server group name"`
	SocketDir           string  `short:"d" long:"socket_dir" description:"Location for all daos_server & daos_engine sockets"`
	Insecure            bool    `short:"i" long:"insecure" description:"Allow for insecure connections"`
	RecreateSuperblocks bool    `long:"recreate-superblocks" description:"Recreate missing superblocks rather than failing"`
	AutoFormat          bool    `long:"auto-format" description:"Automatically format storage on server start to bring-up engines without requiring dmg storage format command"`
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
	if cmd.RecreateSuperblocks {
		cmd.Notice("--recreate-superblocks is deprecated and no longer needed to use externally-managed tmpfs")
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
	log, ok := cmd.Logger.(*logging.LeveledLogger)
	if !ok {
		return errors.New("logger is not a LeveledLogger")
	}

	// Set log level mask for default logger from config,
	// unless it was explicitly set to debug via CLI flag.
	applyLogConfig := func() error {
		switch logging.LogLevel(cmd.config.ControlLogMask) {
		case logging.LogLevelTrace:
			log.SetLevel(logging.LogLevelTrace)
			cmd.Debugf("Switching control log level to TRACE")
		case logging.LogLevelDebug:
			log.SetLevel(logging.LogLevelDebug)
			cmd.Debugf("Switching control log level to DEBUG")
		case logging.LogLevelNotice:
			log.SetLevel(logging.LogLevelNotice)
			cmd.Debugf("Switching control log level to NOTICE")
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
			WithNoticeLogger(logging.NewNoticeLogger(hostname, f)).
			WithInfoLogger(logging.NewInfoLogger(hostname, f)).
			WithDebugLogger(logging.NewDebugLogger(f)).
			WithTraceLogger(logging.NewTraceLogger(f))

		return applyLogConfig()
	}

	cmd.Info("no control log file specified; logging to stdout")

	return applyLogConfig()
}

func (cmd *startCmd) fetchLocalConfig(ctx context.Context) (*config.Server, error) {
	// TODO: Receive access points, provider and use-tmpfs-scm on commandline.
	//       This can be done by embedding cmdutil.ConfGenerateCmd into startCmd.
	cmd.Info("generating config to start server with")

	hf, err := getLocalFabric(ctx, cmd.Logger, "") // Fetch all providers.
	if err != nil {
		return nil, err
	}
	cmd.Debugf("fetched host fabric info on localhost: %+v", hf)

	hs, err := getLocalStorage(ctx, cmd.Logger, false) // Include NVMe-prep.
	if err != nil {
		return nil, err
	}
	cmd.Debugf("fetched host storage info on localhost: %+v", hs)

	req := control.ConfGenerateReq{
		Log: cmd.Logger,
		// TODO: hardcode defaults for the moment until taken from commandline
		NetClass:     hardware.Infiniband,
		AccessPoints: []string{"localhost"},
		// NetProvider:  cmd.NetProvider,
		// AccessPoints: accessPoints,
	}
	if len(hs.ScmNamespaces) == 0 {
		req.UseTmpfsSCM = true
	}

	cmd.Debugf("control API ConfGenerate called with req: %+v", req)

	resp, err := control.ConfGenerate(req, control.DefaultEngineCfg, hf, hs)
	if err != nil {
		return nil, err
	}

	cmd.Debugf("control API ConfGenerate resp: %+v", resp)
	return &resp.Server, nil
}

func (cmd *startCmd) Execute(args []string) error {
	if cmd.start == nil {
		cmd.start = server.Start
	}

	if cmd.AutoFormat {
		// TODO: pass context from here into start
		ctx := context.TODO()
		cfg, err := cmd.fetchLocalConfig(ctx)
		if err != nil {
			return errors.WithMessage(err, "starting with auto config")
		}
		cfg.AutoFormat = true
		// TODO: hardcoded for the moment ???
		cfg.TransportConfig.AllowInsecure = true
		cmd.config = cfg
	}

	if err := cmd.configureLogging(); err != nil {
		return err
	}

	cmd.config.AutoFormat = cmd.AutoFormat

	return cmd.start(cmd.Logger, cmd.config)
}
