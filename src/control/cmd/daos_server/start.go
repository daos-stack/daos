package main

import (
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
)

type serverStarter func(*logging.LeveledLogger, *server.Configuration) error

type startCmd struct {
	logCmd
	cfgCmd
	start       serverStarter
	Port        uint16  `short:"p" long:"port" description:"Port for the gRPC management interfect to listen on"`
	MountPath   string  `short:"s" long:"storage" description:"Storage path"`
	Modules     *string `short:"m" long:"modules" description:"List of server modules to load"`
	Targets     uint16  `short:"t" long:"targets" description:"number of targets to use (default use all cores)"`
	NrXsHelpers *uint16 `short:"x" long:"xshelpernr" description:"number of helper XS per VOS target"`
	FirstCore   uint16  `short:"f" long:"firstcore" default:"0" description:"index of first core for service thread"`
	Group       string  `short:"g" long:"group" description:"Server group name"`
	Attach      *string `short:"a" long:"attach_info" description:"Attach info patch (to support non-PMIx client)"`
	SocketDir   string  `short:"d" long:"socket_dir" description:"Location for all daos_server & daos_io_server sockets"`
	Insecure    bool    `short:"i" long:"insecure" description:"allow for insecure connections"`
}

func (cmd *startCmd) setCLIOverrides() error {
	// Override certificate support if specified in cliOpts
	if cmd.Insecure {
		cmd.config.TransportConfig.AllowInsecure = true
	}
	if cmd.Port > 0 {
		cmd.config.Port = int(cmd.Port)
	}
	if cmd.MountPath != "" {
		cmd.config.ScmMountPath = cmd.MountPath
	}
	if cmd.Group != "" {
		cmd.config.SystemName = cmd.Group
	}
	if cmd.SocketDir != "" {
		cmd.config.SocketDir = cmd.SocketDir
	}
	if cmd.Modules != nil {
		cmd.config.Modules = *cmd.Modules
	}
	if cmd.Attach != nil {
		cmd.config.Attach = *cmd.Attach
	}

	host, err := os.Hostname()
	if err != nil {
		return err
	}
	if cmd.config.NvmeShmID == 0 {
		cmd.config.SetNvmeShmID(host)
	}

	for _, srv := range cmd.config.Servers {
		srv.Hostname = host

		if cmd.MountPath != "" {
			// override each per-server config in addition to global value
			srv.ScmMount = cmd.MountPath
		} else if srv.ScmMount == "" {
			// if scm not specified for server, apply global
			srv.ScmMount = cmd.config.ScmMountPath
		}
		if cmd.Targets > 0 {
			srv.Targets = int(cmd.Targets)
		}
		if cmd.NrXsHelpers != nil {
			srv.NrXsHelpers = int(*cmd.NrXsHelpers)
		}
		if cmd.FirstCore > 0 {
			srv.FirstCore = int(cmd.FirstCore)
		}
	}

	return cmd.config.SetIOServerFlags()
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

	for i, srv := range cmd.config.Servers {
		if srv.LogFile == "" {
			cmd.log.Errorf("no daos log file specified for server %d", i)
		}
	}

	return nil
}

func (cmd *startCmd) Execute(args []string) error {
	if cmd.start == nil {
		cmd.start = server.Start
	}

	if err := cmd.configureLogging(); err != nil {
		return err
	}

	cmd.log.Debugf("cfg: %+v", cmd.config)
	for _, srv := range cmd.config.Servers {
		cmd.log.Debugf("iosrv: %+v", srv)
	}
	return cmd.start(cmd.log, cmd.config)
}
