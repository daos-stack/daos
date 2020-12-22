//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path"

	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

type cliOptions struct {
	AllowProxy bool              `long:"allow-proxy" description:"Allow proxy configuration via environment"`
	Debug      string            `short:"d" long:"debug" optional:"1" optional-value:"basic" choice:"basic" choice:"net" description:"Enable basic or enhanced network debug"`
	JSON       bool              `short:"j" long:"json" description:"Enable JSON output"`
	JSONLogs   bool              `short:"J" long:"json-logging" description:"Enable JSON-formatted log output"`
	ConfigPath string            `short:"o" long:"config-path" description:"Path to agent configuration file"`
	Insecure   bool              `short:"i" long:"insecure" description:"have agent attempt to connect without certificates"`
	RuntimeDir string            `short:"s" long:"runtime_dir" description:"Path to agent communications socket"`
	LogFile    string            `short:"l" long:"logfile" description:"Full path and filename for daos agent log file"`
	Start      startCmd          `command:"start" description:"Start daos_agent daemon (default behavior)"`
	Version    versionCmd        `command:"version" description:"Print daos_agent version"`
	DumpInfo   dumpAttachInfoCmd `command:"dump-attachinfo" description:"Dump system attachinfo"`
	NetScan    netScanCmd        `command:"net-scan" description:"Perform local network fabric scan"`
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

type (
	logSetter interface {
		setLog(logging.Logger)
	}

	logCmd struct {
		log logging.Logger
	}
)

func (cmd *logCmd) setLog(log logging.Logger) {
	cmd.log = log
}

type (
	jsonOutputter interface {
		enableJsonOutput(bool)
		jsonOutputEnabled() bool
		outputJSON(io.Writer, interface{}) error
	}

	jsonOutputCmd struct {
		shouldEmitJSON bool
	}
)

func (cmd *jsonOutputCmd) enableJsonOutput(emitJson bool) {
	cmd.shouldEmitJSON = emitJson
}

func (cmd *jsonOutputCmd) jsonOutputEnabled() bool {
	return cmd.shouldEmitJSON
}

func (cmd *jsonOutputCmd) outputJSON(out io.Writer, in interface{}) error {
	data, err := json.MarshalIndent(in, "", "  ")
	if err != nil {
		return err
	}

	_, err = out.Write(append(data, []byte("\n")...))
	return err
}

func versionString() string {
	return fmt.Sprintf("%s v%s", build.AgentName, build.DaosVersion)
}

type versionCmd struct{}

func (cmd *versionCmd) Execute(_ []string) error {
	_, err := fmt.Println(versionString())
	return err
}

func exitWithError(log logging.Logger, err error) {
	log.Errorf("%s: %v", path.Base(os.Args[0]), err)
	os.Exit(1)
}

func parseOpts(args []string, opts *cliOptions, invoker control.Invoker, log *logging.LeveledLogger) error {
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

		if logCmd, ok := cmd.(logSetter); ok {
			logCmd.setLog(log)
		}

		if jsonCmd, ok := cmd.(jsonOutputter); ok {
			jsonCmd.enableJsonOutput(opts.JSON)
		}

		switch opts.Debug {
		case "net":
			log.WithLogLevel(logging.LogLevelDebug)
			log.Debug("net debug output enabled")
			netdetect.SetLogger(log)
		case "basic":
			log.WithLogLevel(logging.LogLevelDebug)
			log.Debug("basic debug output enabled")
		}

		if opts.JSONLogs {
			log.WithJSONOutput()
		}

		switch cmd.(type) {
		case *versionCmd, *netScanCmd:
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
			log.Debugf("agent config loaded from %s", cfgPath)
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
				WithInfoLogger(logging.NewInfoLogger("agent", f)).
				WithDebugLogger(logging.NewDebugLogger(f))
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

		if err := cmd.Execute(args); err != nil {
			return err
		}

		return nil
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
		exitWithError(log, err)
	}
}
