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
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
)

type serverStarter func(*logging.LeveledLogger, *config.Server) error

type startCmd struct {
	logCmd
	cfgCmd
	start               serverStarter
	Port                uint16  `short:"p" long:"port" description:"Port for the gRPC management interfect to listen on"`
	MountPath           string  `short:"s" long:"storage" description:"Storage path"`
	Modules             *string `short:"m" long:"modules" description:"List of server modules to load"`
	Targets             uint16  `short:"t" long:"targets" description:"number of targets to use (default use all cores)"`
	NrXsHelpers         *uint16 `short:"x" long:"xshelpernr" description:"number of helper XS per VOS target"`
	FirstCore           uint16  `short:"f" long:"firstcore" default:"0" description:"index of first core for service thread"`
	Group               string  `short:"g" long:"group" description:"Server group name"`
	SocketDir           string  `short:"d" long:"socket_dir" description:"Location for all daos_server & daos_io_server sockets"`
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
		cmd.config.WithScmMountPoint(cmd.MountPath)
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

	host, err := os.Hostname()
	if err != nil {
		return err
	}

	for _, srv := range cmd.config.Servers {
		srv.WithHostname(host)

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

	return cmd.config.Validate(cmd.log)
}

func (cmd *startCmd) configureLogging() error {
	// Set log level mask for default logger from config,
	// unless it was explicitly set to debug via CLI flag.
	applyLogConfig := func() {
		switch logging.LogLevel(cmd.config.ControlLogMask) {
		case logging.LogLevelDebug:
			cmd.log.SetLevel(logging.LogLevelDebug)
			cmd.log.Debugf("Switching control log level to DEBUG")
		case logging.LogLevelError:
			cmd.log.Debugf("Switching control log level to ERROR")
			cmd.log.SetLevel(logging.LogLevelError)
		}

		if cmd.config.ControlLogJSON {
			cmd.log = cmd.log.WithJSONOutput()
		}
	}

	hostname, err := os.Hostname()
	if err != nil {
		return err
	}

	for i, srv := range cmd.config.Servers {
		if srv.LogFile == "" {
			cmd.log.Errorf("no daos log file specified for server %d", i)
		}
	}

	// Set log file for default logger if specified in config.
	if cmd.config.ControlLogFile != "" {
		f, err := common.AppendFile(cmd.config.ControlLogFile)
		if err != nil {
			return errors.WithMessage(err, "create log file")
		}

		cmd.log.Infof("%s logging to file %s",
			os.Args[0], cmd.config.ControlLogFile)
		// Create an additional set of loggers which append everything
		// to the specified file.
		cmd.log = cmd.log.
			WithErrorLogger(logging.NewErrorLogger(hostname, f)).
			WithInfoLogger(logging.NewInfoLogger(hostname, f)).
			WithDebugLogger(logging.NewDebugLogger(f))
		applyLogConfig()

		return nil
	}

	cmd.log.Info("no control log file specified; logging to stdout")
	applyLogConfig()

	return nil
}

func (cmd *startCmd) Execute(args []string) error {
	if cmd.start == nil {
		cmd.start = server.Start
	}

	if err := cmd.configureLogging(); err != nil {
		return err
	}

	return cmd.start(cmd.log, cmd.config)
}
