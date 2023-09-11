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
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	hostListGetter interface {
		getHostList() []string
	}

	hostListSetter interface {
		setHostList(*hostlist.HostSet)
	}

	hostListCmd struct {
		HostList ui.HostSetFlag `short:"l" long:"host-list" description:"A comma separated list of addresses <ipv4addr/hostname> to connect to"`
		hostlist []string
	}

	singleHostCmd struct {
		HostList singleHostFlag `short:"l" long:"host-list" default:"localhost" description:"Single host address <ipv4addr/hostname> to connect to"`
		host     string
	}

	ctlInvoker interface {
		setInvoker(control.Invoker)
	}

	ctlInvokerCmd struct {
		ctlInvoker control.Invoker
	}
)

func (cmd *ctlInvokerCmd) setInvoker(c control.Invoker) {
	cmd.ctlInvoker = c
}

func (cmd *hostListCmd) getHostList() []string {
	if cmd.hostlist == nil && !cmd.HostList.Empty() {
		cmd.hostlist = cmd.HostList.Slice()
	}
	return cmd.hostlist
}

func (cmd *hostListCmd) setHostList(newList *hostlist.HostSet) {
	cmd.HostList.Replace(newList)
}

func (cmd *singleHostCmd) getHostList() []string {
	if cmd.host == "" {
		if cmd.HostList.Count() == 0 {
			cmd.host = "localhost"
		} else {
			cmd.host = cmd.HostList.Slice()[0]
		}
	}
	return []string{cmd.host}
}

func (cmd *singleHostCmd) setHostList(newList *hostlist.HostSet) {
	cmd.HostList.Replace(newList)
}

type cmdLogger interface {
	setLog(*logging.LeveledLogger)
}

type baseCmd struct {
	cmdutil.NoArgsCmd
	cmdutil.LogCmd
}

// cmdConfigSetter is an interface for setting the control config on a command
type cmdConfigSetter interface {
	setConfig(*control.Config)
}

// cfgCmd is a structure that can be used by commands that need the control
// config.
type cfgCmd struct {
	config *control.Config
}

func (c *cfgCmd) setConfig(cfg *control.Config) {
	c.config = cfg
}

type cliOptions struct {
	AllowProxy     bool           `long:"allow-proxy" description:"Allow proxy configuration via environment"`
	HostList       ui.HostSetFlag `short:"l" long:"host-list" hidden:"true" description:"DEPRECATED: A comma separated list of addresses <ipv4addr/hostname> to connect to"`
	Insecure       bool           `short:"i" long:"insecure" description:"Have dmg attempt to connect without certificates"`
	Debug          bool           `short:"d" long:"debug" description:"Enable debug output"`
	LogFile        string         `long:"log-file" description:"Log command output to the specified file"`
	JSON           bool           `short:"j" long:"json" description:"Enable JSON output"`
	JSONLogs       bool           `short:"J" long:"json-logging" description:"Enable JSON-formatted log output"`
	ConfigPath     string         `short:"o" long:"config-path" description:"Client config file path"`
	Server         serverCmd      `command:"server" alias:"srv" description:"Perform tasks related to remote servers"`
	Storage        storageCmd     `command:"storage" alias:"sto" description:"Perform tasks related to storage attached to remote servers"`
	Config         configCmd      `command:"config" alias:"cfg" description:"Perform tasks related to configuration of hardware on remote servers"`
	System         SystemCmd      `command:"system" alias:"sys" description:"Perform distributed tasks related to DAOS system"`
	Network        NetCmd         `command:"network" alias:"net" description:"Perform tasks related to network devices attached to remote servers"`
	Support        supportCmd     `command:"support" alias:"supp" description:"Perform debug tasks to help support team"`
	Pool           PoolCmd        `command:"pool" description:"Perform tasks related to DAOS pools"`
	Cont           ContCmd        `command:"container" alias:"cont" description:"Perform tasks related to DAOS containers"`
	Version        versionCmd     `command:"version" description:"Print dmg version"`
	Telemetry      telemCmd       `command:"telemetry" alias:"telem" description:"Perform telemetry operations"`
	firmwareOption                // build with tag "firmware" to enable
	ManPage        cmdutil.ManCmd `command:"manpage" hidden:"true"`
}

type versionCmd struct {
	cmdutil.JSONOutputCmd
}

func (cmd *versionCmd) Execute(_ []string) error {
	if cmd.JSONOutputEnabled() {
		buf, err := build.MarshalJSON(build.AdminUtilName)
		if err != nil {
			return err
		}
		return cmd.OutputJSON(json.RawMessage(buf), nil)
	}

	fmt.Println(build.String(build.AdminUtilName))
	os.Exit(0)
	return nil
}

func exitWithError(log logging.Logger, err error) {
	cmdName := path.Base(os.Args[0])
	log.Errorf("%s: %v", cmdName, err)
	if fault.HasResolution(err) {
		log.Errorf("%s: %s", cmdName, fault.ShowResolutionFor(err))
	}
	os.Exit(1)
}

func parseOpts(args []string, opts *cliOptions, invoker control.Invoker, log *logging.LeveledLogger) error {
	var wroteJSON atm.Bool
	p := flags.NewParser(opts, flags.Default)
	p.Name = "dmg"
	p.ShortDescription = "Administrative tool for managing DAOS clusters"
	p.LongDescription = `dmg (DAOS Management) is a tool for connecting to DAOS servers
for the purpose of issuing administrative commands to the cluster. dmg is
provided as a means for allowing administrators to securely discover and
administer DAOS components such as storage allocations, network configuration,
and access control settings, along with system wide operations.`
	p.Options ^= flags.PrintErrors // Don't allow the library to print errors
	p.CommandHandler = func(cmd flags.Commander, args []string) error {
		if cmd == nil {
			return nil
		}

		if manCmd, ok := cmd.(cmdutil.ManPageWriter); ok {
			manCmd.SetWriteFunc(p.WriteManPage)
			// Just execute now without any more setup.
			return cmd.Execute(args)
		}

		if !opts.AllowProxy {
			common.ScrubProxyVariables()
		}

		if opts.LogFile != "" {
			f, err := common.AppendFile(opts.LogFile)
			if err != nil {
				return errors.WithMessage(err, "create log file")
			}
			defer f.Close()

			log.Debugf("%s logging to file %s",
				os.Args[0], opts.LogFile)

			// Create an additional set of loggers which append everything
			// to the specified file.
			log = log.
				WithErrorLogger(logging.NewErrorLogger("dmg", f)).
				WithNoticeLogger(logging.NewNoticeLogger("dmg", f)).
				WithInfoLogger(logging.NewInfoLogger("dmg", f)).
				WithDebugLogger(logging.NewDebugLogger(f)).
				WithTraceLogger(logging.NewTraceLogger(f))
		}

		if opts.Debug {
			log.SetLevel(logging.LogLevelTrace)
			log.Debug("debug output enabled")
		}

		if opts.JSONLogs {
			log.WithJSONOutput()
		}

		if jsonCmd, ok := cmd.(cmdutil.JSONOutputter); ok && opts.JSON {
			jsonCmd.EnableJSONOutput(os.Stdout, &wroteJSON)
			// disable output on stdout other than JSON
			log.ClearLevel(logging.LogLevelInfo)
		}

		if logCmd, ok := cmd.(cmdutil.LogSetter); ok {
			logCmd.SetLog(log)
		}

		switch cmd.(type) {
		case *versionCmd:
			// this command don't need the rest of the setup
			return cmd.Execute(args)
		}

		ctlCfg, err := control.LoadConfig(opts.ConfigPath)
		if err != nil {
			if errors.Cause(err) != control.ErrNoConfigFile {
				return errors.Wrap(err, "failed to load control configuration")
			}
			// Use the default config if no config file was found.
			ctlCfg = control.DefaultConfig()
		}
		if ctlCfg.Path != "" {
			log.Debugf("control config loaded from %s", ctlCfg.Path)
		}

		if opts.Insecure {
			ctlCfg.TransportConfig.AllowInsecure = true
		}
		if err := ctlCfg.TransportConfig.PreLoadCertData(); err != nil {
			return errors.Wrap(err, "Unable to load Certificate Data")
		}

		invoker.SetConfig(ctlCfg)
		if ctlCmd, ok := cmd.(ctlInvoker); ok {
			ctlCmd.setInvoker(invoker)
		}

		// Handle the deprecated global hostlist flag
		if !opts.HostList.Empty() {
			if hlCmd, ok := cmd.(hostListSetter); ok {
				hlCmd.setHostList(&opts.HostList.HostSet)
			} else {
				return &flags.Error{
					Type:    flags.ErrUnknownFlag,
					Message: "unknown flag `l'/`host-list'",
				}
			}
		}

		if hlCmd, ok := cmd.(hostListGetter); ok {
			hl := hlCmd.getHostList()
			if len(hl) > 0 {
				ctlCfg.HostList = hl
			}
		}

		if cfgCmd, ok := cmd.(cmdConfigSetter); ok {
			cfgCmd.setConfig(ctlCfg)
		}

		if argsCmd, ok := cmd.(cmdutil.ArgsHandler); ok {
			if err := argsCmd.CheckArgs(args); err != nil {
				return err
			}
		}

		if err := cmd.Execute(args); err != nil {
			return err
		}

		return nil
	}

	_, err := p.ParseArgs(args)
	if opts.JSON && wroteJSON.IsFalse() {
		return cmdutil.OutputJSON(os.Stdout, nil, err)
	}
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
