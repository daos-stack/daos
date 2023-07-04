//
// (C) Copyright 2018-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path"

	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
)

type cliOptions struct {
	AllowProxy bool                   `long:"allow-proxy" description:"Allow proxy configuration via environment"`
	Debug      bool                   `short:"d" long:"debug" description:"Enable debug output"`
	JSON       bool                   `short:"j" long:"json" description:"Enable JSON output"`
	JSONLogs   bool                   `short:"J" long:"json-logging" description:"Enable JSON-formatted log output"`
	ConfigPath string                 `short:"o" long:"config-path" description:"Path to agent configuration file"`
	Insecure   bool                   `short:"i" long:"insecure" description:"have agent attempt to connect without certificates"`
	RuntimeDir string                 `short:"s" long:"runtime_dir" description:"Path to agent communications socket"`
	LogFile    string                 `short:"l" long:"logfile" description:"Full path and filename for daos agent log file"`
	Start      startCmd               `command:"start" description:"Start daos_agent daemon (default behavior)"`
	Version    versionCmd             `command:"version" description:"Print daos_agent version"`
	DumpInfo   dumpAttachInfoCmd      `command:"dump-attachinfo" description:"Dump system attachinfo"`
	DumpTopo   hwprov.DumpTopologyCmd `command:"dump-topology" description:"Dump system topology"`
	NetScan    netScanCmd             `command:"net-scan" description:"Perform local network fabric scan"`
	Support    supportCmd             `command:"support" description:"Perform debug tasks to help support team"`
}

type (
	ctlInvoker interface {
		setInvoker(control.Invoker)
	}

	ctlInvokerCmd struct {
		ctlInvoker control.Invoker
	}
)

func (cmd *ctlInvokerCmd) setInvoker(ctlInvoker control.Invoker) {
	cmd.ctlInvoker = ctlInvoker
}

type (
	configSetter interface {
		setConfig(*Config)
	}

	configCmd struct {
		cfg *Config
	}
)

func (cmd *configCmd) setConfig(cfg *Config) {
	cmd.cfg = cfg
}

func versionString() string {
	return build.String(build.AgentName)
}

type versionCmd struct {
	cmdutil.JSONOutputCmd
}

func (cmd *versionCmd) Execute(_ []string) error {
	if cmd.JSONOutputEnabled() {
		buf, err := build.MarshalJSON(build.AgentName)
		if err != nil {
			return err
		}
		return cmd.OutputJSON(json.RawMessage(buf), nil)
	}

	_, err := fmt.Println(versionString())
	return err
}

func exitWithError(log logging.Logger, err error) {
	log.Errorf("%s: %v", path.Base(os.Args[0]), err)
	os.Exit(1)
}

type (
	supportAgentConfig interface {
		setSupportConf(string)
		getSupportConf() string
	}

	supportAgentConfigCmd struct {
		supportCfgPath string
	}
)

func (cmd *supportAgentConfigCmd) setSupportConf(cfgPath string) {
	cmd.supportCfgPath = cfgPath
}

func (cmd *supportAgentConfigCmd) getSupportConf() string {
	return cmd.supportCfgPath
}

func parseOpts(args []string, opts *cliOptions, invoker control.Invoker, log *logging.LeveledLogger) error {
	var wroteJSON atm.Bool
	p := flags.NewParser(opts, flags.Default)
	p.Options ^= flags.PrintErrors // Don't allow the library to print errors
	p.SubcommandsOptional = true
	p.CommandHandler = func(cmd flags.Commander, args []string) error {
		if len(args) > 0 {
			exitWithError(log, errors.Errorf("unknown command %q", args[0]))
		}

		if cmd == nil {
			cmd = &startCmd{}
		}

		if logCmd, ok := cmd.(cmdutil.LogSetter); ok {
			logCmd.SetLog(log)
		}

		if jsonCmd, ok := cmd.(cmdutil.JSONOutputter); ok && opts.JSON {
			jsonCmd.EnableJSONOutput(os.Stdout, &wroteJSON)
			// disable output on stdout other than JSON
			log.ClearLevel(logging.LogLevelInfo)
		}

		if opts.Debug {
			log.SetLevel(logging.LogLevelTrace)
		}

		if opts.JSONLogs {
			log.WithJSONOutput()
		}

		switch cmd.(type) {
		case *versionCmd, *netScanCmd, *hwprov.DumpTopologyCmd:
			// these commands don't need the rest of the setup
			return cmd.Execute(args)
		}

		if !opts.AllowProxy {
			common.ScrubProxyVariables()
		}

		cfgPath := opts.ConfigPath
		if cfgPath == "" {
			defaultConfigPath := path.Join(build.ConfigDir, defaultConfigFile)
			if _, err := os.Stat(defaultConfigPath); err == nil {
				cfgPath = defaultConfigPath
			}
		}

		cfg := DefaultConfig()
		if cfgPath != "" {
			var err error
			if cfg, err = LoadConfig(cfgPath); err != nil {
				return errors.WithMessage(err, "failed to load agent configuration")
			}

			// Command line debug option overrides log level in config file
			if !opts.Debug {
				log.WithLogLevel(logging.LogLevel(cfg.LogLevel))
			}
			log.Debugf("agent config loaded from %s", cfgPath)
		}

		if suppCmd, ok := cmd.(supportAgentConfig); ok {
			suppCmd.setSupportConf(cfgPath)
		}

		if opts.RuntimeDir != "" {
			log.Debugf("Overriding socket path from config file with %s", opts.RuntimeDir)
			cfg.RuntimeDir = opts.RuntimeDir
		}

		if opts.LogFile != "" {
			log.Debugf("Overriding LogFile path from config file with %s", opts.LogFile)
			cfg.LogFile = opts.LogFile
		}

		if opts.Insecure {
			log.Debugf("Overriding AllowInsecure from config file with %t", opts.Insecure)
			cfg.TransportConfig.AllowInsecure = true
		}

		if cfg.LogFile != "" {
			f, err := common.AppendFile(cfg.LogFile)
			if err != nil {
				log.Errorf("Failure creating log file: %s", err)
				return err
			}
			defer f.Close()

			// Create an additional set of loggers which append everything
			// to the specified file.
			log.WithErrorLogger(logging.NewErrorLogger("agent", f)).
				WithNoticeLogger(logging.NewNoticeLogger("agent", f)).
				WithInfoLogger(logging.NewInfoLogger("agent", f)).
				WithDebugLogger(logging.NewDebugLogger(f)).
				WithTraceLogger(logging.NewTraceLogger(f))
		}

		if err := cfg.TransportConfig.PreLoadCertData(); err != nil {
			return errors.Wrap(err, "Unable to load Certificate Data")
		}

		var err error
		if cfg.AccessPoints, err = common.ParseHostList(cfg.AccessPoints, cfg.ControlPort); err != nil {
			return errors.Wrap(err, "Failed to parse config access_points")
		}

		if cfgCmd, ok := cmd.(configSetter); ok {
			cfgCmd.setConfig(cfg)
		}

		if ctlCmd, ok := cmd.(ctlInvoker); ok {
			// Generate a control config based on the loaded agent config.
			ctlCfg := control.DefaultConfig()
			ctlCfg.TransportConfig = cfg.TransportConfig
			ctlCfg.HostList = cfg.AccessPoints
			ctlCfg.SystemName = cfg.SystemName
			ctlCfg.ControlPort = cfg.ControlPort

			invoker.SetConfig(ctlCfg)
			ctlCmd.setInvoker(invoker)
		}

		err = cmd.Execute(args)
		if opts.JSON && wroteJSON.IsFalse() {
			cmdutil.OutputJSON(os.Stdout, nil, err)
		}

		return err
	}

	_, err := p.Parse()
	return err
}

func main() {
	var opts cliOptions
	log := logging.NewCommandLineLogger()

	ctlInvoker := control.NewClient(
		control.WithClientLogger(log),
	)

	if err := parseOpts(os.Args[1:], &opts, ctlInvoker, log); err != nil {
		if fe, ok := errors.Cause(err).(*flags.Error); ok && fe.Type == flags.ErrHelp {
			log.Info(fe.Error())
			os.Exit(0)
		}
		exitWithError(log, err)
	}
}
