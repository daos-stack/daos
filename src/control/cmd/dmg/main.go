//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path"
	"strings"

	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	hostListSetter interface {
		setHostList([]string)
	}

	hostListCmd struct {
		hostlist []string
	}

	ctlInvoker interface {
		setInvoker(control.Invoker)
	}

	ctlInvokerCmd struct {
		ctlInvoker control.Invoker
	}

	jsonOutputter interface {
		enableJsonOutput(bool, io.Writer, *atm.Bool)
		jsonOutputEnabled() bool
		outputJSON(interface{}, error) error
		errorJSON(error) error
	}

	jsonOutputCmd struct {
		wroteJSON      *atm.Bool
		writer         io.Writer
		shouldEmitJSON bool
	}
)

func (cmd *ctlInvokerCmd) setInvoker(c control.Invoker) {
	cmd.ctlInvoker = c
}

func (cmd *hostListCmd) setHostList(hl []string) {
	cmd.hostlist = hl
}

func (cmd *jsonOutputCmd) enableJsonOutput(emitJson bool, w io.Writer, wj *atm.Bool) {
	cmd.shouldEmitJSON = emitJson
	cmd.writer = w
	cmd.wroteJSON = wj
}

func (cmd *jsonOutputCmd) jsonOutputEnabled() bool {
	return cmd.shouldEmitJSON
}

func outputJSON(out io.Writer, in interface{}, cmdErr error) error {
	status := 0
	var errStr *string
	if cmdErr != nil {
		errStr = new(string)
		*errStr = cmdErr.Error()
		if s, ok := errors.Cause(cmdErr).(drpc.DaosStatus); ok {
			status = int(s)
		} else {
			status = int(drpc.DaosMiscError)
		}
	}

	data, err := json.MarshalIndent(struct {
		Response interface{} `json:"response"`
		Error    *string     `json:"error"`
		Status   int         `json:"status"`
	}{in, errStr, status}, "", "  ")
	if err != nil {
		fmt.Fprintf(out, "unable to marshal json: %s\n", err.Error())
		return err
	}

	if _, err = out.Write(append(data, []byte("\n")...)); err != nil {
		fmt.Fprintf(out, "unable to write json: %s\n", err.Error())
		return err
	}

	return cmdErr
}

func (cmd *jsonOutputCmd) outputJSON(in interface{}, cmdErr error) error {
	if cmd.wroteJSON.IsTrue() {
		return cmdErr
	}
	cmd.wroteJSON.SetTrue()
	return outputJSON(cmd.writer, in, cmdErr)
}

func errorJSON(err error) error {
	return outputJSON(os.Stdout, nil, err)
}

func (cmd *jsonOutputCmd) errorJSON(err error) error {
	return cmd.outputJSON(nil, err)
}

var _ jsonOutputter = (*jsonOutputCmd)(nil)

type cmdLogger interface {
	setLog(*logging.LeveledLogger)
}

type logCmd struct {
	cmdutil.NoArgsCmd // Hack: Since everything embeds this type,
	// just embed the args handler in here. We should probably
	// create a new baseCmd type to cut down on boilerplate.
	log *logging.LeveledLogger
}

func (c *logCmd) setLog(log *logging.LeveledLogger) {
	c.log = log
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
	HostList       string         `short:"l" long:"host-list" description:"A comma separated list of addresses <ipv4addr/hostname> to connect to"`
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
	Pool           PoolCmd        `command:"pool" description:"Perform tasks related to DAOS pools"`
	Cont           ContCmd        `command:"container" alias:"cont" description:"Perform tasks related to DAOS containers"`
	Version        versionCmd     `command:"version" description:"Print dmg version"`
	Telemetry      telemCmd       `command:"telemetry" alias:"telem" description:"Perform telemetry operations"`
	firmwareOption                // build with tag "firmware" to enable
	ManPage        cmdutil.ManCmd `command:"manpage" hidden:"true"`
}

type versionCmd struct{}

func (cmd *versionCmd) Execute(_ []string) error {
	fmt.Printf("dmg version %s\n", build.DaosVersion)
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
				WithInfoLogger(logging.NewInfoLogger("dmg", f)).
				WithDebugLogger(logging.NewDebugLogger(f))
		}

		if opts.Debug {
			log.WithLogLevel(logging.LogLevelDebug)
			log.Debug("debug output enabled")
		}

		if opts.JSONLogs {
			log.WithJSONOutput()
		}

		if jsonCmd, ok := cmd.(jsonOutputter); ok {
			jsonCmd.enableJsonOutput(opts.JSON, os.Stdout, &wroteJSON)
			if opts.JSON {
				// disable output on stdout other than JSON
				log.ClearLevel(logging.LogLevelInfo)
			}
		}

		if logCmd, ok := cmd.(cmdLogger); ok {
			logCmd.setLog(log)
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

		if opts.HostList != "" {
			if hlCmd, ok := cmd.(hostListSetter); ok {
				hl := strings.Split(opts.HostList, ",")
				hlCmd.setHostList(hl)
				ctlCfg.HostList = hl
			} else {
				return errors.Errorf("this command does not accept a hostlist parameter (set it in %s or %s)",
					control.UserConfigPath(), control.SystemConfigPath())
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
		return errorJSON(err)
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
