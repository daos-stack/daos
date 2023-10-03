//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

const defaultConfigFile = "daos_server.yml"

type execTestFn func() error

type mainOpts struct {
	AllowProxy bool `long:"allow-proxy" description:"Allow proxy configuration via environment"`
	// Minimal set of top-level options
	ConfigPath string `short:"o" long:"config" description:"Server config file path"`
	// TODO(DAOS-3129): This should be -d, but it conflicts with the start
	// subcommand's -d flag when we default to running it.
	Debug   bool `short:"b" long:"debug" description:"Enable debug output"`
	JSON    bool `long:"json" short:"j" description:"enable JSON output"`
	JSONLog bool `short:"J" long:"json-logging" description:"Enable JSON-formatted log output"`
	Syslog  bool `long:"syslog" description:"Enable logging to syslog"`

	// Define subcommands
	SCM           scmStorageCmd          `command:"scm" description:"Perform tasks related to locally-attached SCM storage"`
	NVMe          nvmeStorageCmd         `command:"nvme" description:"Perform tasks related to locally-attached NVMe storage"`
	LegacyStorage legacyStorageCmd       `command:"storage" description:"Perform tasks related to locally-attached storage (deprecated, use scm or nvme instead)"`
	Start         startCmd               `command:"start" description:"Start daos_server"`
	Network       networkCmd             `command:"network" description:"Perform network device scan based on fabric provider"`
	Version       versionCmd             `command:"version" description:"Print daos_server version"`
	MgmtSvc       msCmdRoot              `command:"ms" description:"Perform tasks related to management service replicas"`
	DumpTopo      hwprov.DumpTopologyCmd `command:"dump-topology" description:"Dump system topology"`
	Support       supportCmd             `command:"support" description:"Perform debug tasks to help support team"`
	Config        configCmd              `command:"config" alias:"cfg" description:"Perform tasks related to configuration of hardware on the local server"`

	// Allow a set of tests to be run before executing commands.
	preExecTests []execTestFn
}

type versionCmd struct {
	cmdutil.JSONOutputCmd
}

func (cmd *versionCmd) Execute(_ []string) error {
	if cmd.JSONOutputEnabled() {
		buf, err := build.MarshalJSON(build.ControlPlaneName)
		if err != nil {
			return err
		}
		return cmd.OutputJSON(json.RawMessage(buf), nil)
	}

	fmt.Println(build.String(build.ControlPlaneName))
	return nil
}

func exitWithError(log *logging.LeveledLogger, err error) {
	log.Debugf("%+v", err)
	log.Errorf("%v", err)
	if fault.HasResolution(err) {
		log.Error(fault.ShowResolutionFor(err))
	}
	os.Exit(1)
}

func parseOpts(args []string, opts *mainOpts, log *logging.LeveledLogger) error {
	var wroteJSON atm.Bool
	p := flags.NewParser(opts, flags.HelpFlag|flags.PassDoubleDash)
	p.SubcommandsOptional = false
	p.CommandHandler = func(cmd flags.Commander, cmdArgs []string) error {
		if len(cmdArgs) > 0 {
			// don't support positional arguments, extra cmdArgs are unexpected
			return errors.Errorf("unexpected commandline arguments: %v", cmdArgs)
		}

		if jsonCmd, ok := cmd.(cmdutil.JSONOutputter); ok && opts.JSON {
			jsonCmd.EnableJSONOutput(os.Stdout, &wroteJSON)
			// disable output on stdout other than JSON
			log.ClearLevel(logging.LogLevelInfo)
		}

		switch cmd.(type) {
		case *versionCmd:
			// No pre-exec tests or setup needed for these commands; just
			// execute them directly.
			return cmd.Execute(nil)
		default:
			for _, test := range opts.preExecTests {
				if err := test(); err != nil {
					return err
				}
			}
		}

		if !opts.AllowProxy {
			common.ScrubProxyVariables()
		}
		if opts.Debug {
			log.SetLevel(logging.LogLevelTrace)
		}
		if opts.JSONLog {
			log.WithJSONOutput()
		}
		if opts.Syslog {
			// Don't log debug stuff to syslog.
			log.WithInfoLogger((&logging.DefaultInfoLogger{}).WithSyslogOutput())
			log.WithNoticeLogger((&logging.DefaultNoticeLogger{}).WithSyslogOutput())
			log.WithErrorLogger((&logging.DefaultErrorLogger{}).WithSyslogOutput())
		}

		if logCmd, ok := cmd.(cmdutil.LogSetter); ok {
			logCmd.SetLog(log)
		}

		if cfgCmd, ok := cmd.(cfgLoader); ok {
			if opts.ConfigPath == "" {
				log.Debugf("Using build config directory %q", build.ConfigDir)
				opts.ConfigPath = path.Join(build.ConfigDir, defaultConfigFile)
			}

			if err := cfgCmd.loadConfig(opts.ConfigPath); err != nil {
				return errors.Wrapf(err, "failed to load config from %s", cfgCmd.configPath())
			}
			if _, err := os.Stat(opts.ConfigPath); err == nil {
				log.Infof("DAOS Server config loaded from %s", cfgCmd.configPath())
			}

			if ovrCmd, ok := cfgCmd.(cliOverrider); ok {
				if err := ovrCmd.setCLIOverrides(); err != nil {
					return errors.Wrap(err, "failed to set CLI config overrides")
				}
			}
		} else if opts.ConfigPath != "" {
			return errors.Errorf("DAOS Server config filepath has been supplied but " +
				"this command will not use it")
		}

		if err := cmd.Execute(cmdArgs); err != nil {
			return err
		}

		return nil
	}

	// Parse commandline flags which override options loaded from config.
	_, err := p.ParseArgs(args)
	if opts.JSON && wroteJSON.IsFalse() {
		cmdutil.OutputJSON(os.Stdout, nil, err)
	}

	return err
}

func main() {
	log := logging.NewCommandLineLogger()
	opts := mainOpts{
		preExecTests: []execTestFn{
			// Check that the privileged helper is installed and working.
			func() error {
				return pbin.CheckHelper(log, pbin.DaosPrivHelperName)
			},
		},
	}

	if err := parseOpts(os.Args[1:], &opts, log); err != nil {
		if errors.Cause(err) == context.Canceled {
			log.Infof("%s (pid %d) shutting down", build.ControlPlaneName, os.Getpid())
			os.Exit(0)
		}
		if fe, ok := errors.Cause(err).(*flags.Error); ok && fe.Type == flags.ErrHelp {
			log.Info(fe.Error())
			os.Exit(0)
		}
		exitWithError(log, err)
	}
}
