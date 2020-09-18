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
	"strings"

	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
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
		enableJsonOutput(bool, io.Writer)
		jsonOutputEnabled() bool
		outputJSON(interface{}, error) error
		errorJSON(error) error
	}

	jsonOutputCmd struct {
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

func (cmd *jsonOutputCmd) enableJsonOutput(emitJson bool, w io.Writer) {
	cmd.shouldEmitJSON = emitJson
	cmd.writer = w
}

func (cmd *jsonOutputCmd) jsonOutputEnabled() bool {
	return cmd.shouldEmitJSON
}

func (cmd *jsonOutputCmd) outputJSON(in interface{}, cmdErr error) error {
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
		return err
	}

	if _, err = cmd.writer.Write(append(data, []byte("\n")...)); err != nil {
		return err
	}

	return cmdErr
}

func (cmd *jsonOutputCmd) errorJSON(err error) error {
	return cmd.outputJSON(nil, err)
}

var _ jsonOutputter = (*jsonOutputCmd)(nil)

type cmdLogger interface {
	setLog(*logging.LeveledLogger)
}

type logCmd struct {
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
	AllowProxy     bool       `long:"allow-proxy" description:"Allow proxy configuration via environment"`
	HostList       string     `short:"l" long:"host-list" description:"comma separated list of addresses <ipv4addr/hostname:port>"`
	Insecure       bool       `short:"i" long:"insecure" description:"have dmg attempt to connect without certificates"`
	Debug          bool       `short:"d" long:"debug" description:"enable debug output"`
	JSON           bool       `short:"j" long:"json" description:"Enable JSON output"`
	JSONLogs       bool       `short:"J" long:"json-logging" description:"Enable JSON-formatted log output"`
	ConfigPath     string     `short:"o" long:"config-path" description:"Client config file path"`
	Storage        storageCmd `command:"storage" alias:"st" description:"Perform tasks related to storage attached to remote servers"`
	System         SystemCmd  `command:"system" alias:"sy" description:"Perform distributed tasks related to DAOS system"`
	Network        NetCmd     `command:"network" alias:"n" description:"Perform tasks related to network devices attached to remote servers"`
	Pool           PoolCmd    `command:"pool" alias:"p" description:"Perform tasks related to DAOS pools"`
	Cont           ContCmd    `command:"cont" alias:"c" description:"Perform tasks related to DAOS containers"`
	Version        versionCmd `command:"version" description:"Print dmg version"`
	firmwareOption            // build with tag "firmware" to enable
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

func writeManPage(wr io.Writer) {
	var opts cliOptions
	p := flags.NewParser(&opts, flags.Default)
	p.Name = "dmg"
	p.ShortDescription = "Administrative tool for managing DAOS clusters"
	p.Usage = "[OPTIONS] [COMMAND] [SUBCOMMAND]"
	p.LongDescription = `dmg (DAOS Management) is a tool for connecting to DAOS servers
for the purpose of issuing administrative commands to the cluster. dmg is
provided as a means for allowing administrators to securely discover and
administer DAOS components such as storage allocations, network configuration,
and access control settings, along with system wide operations.`
	p.WriteManPage(wr)
}

func parseOpts(args []string, opts *cliOptions, invoker control.Invoker, log *logging.LeveledLogger) error {
	p := flags.NewParser(opts, flags.Default)
	p.Options ^= flags.PrintErrors // Don't allow the library to print errors
	p.CommandHandler = func(cmd flags.Commander, args []string) error {
		if cmd == nil {
			return nil
		}

		if !opts.AllowProxy {
			common.ScrubProxyVariables()
		}

		if opts.Debug {
			log.WithLogLevel(logging.LogLevelDebug)
			log.Debug("debug output enabled")
		}

		if opts.JSONLogs {
			log.WithJSONOutput()
		}

		if jsonCmd, ok := cmd.(jsonOutputter); ok {
			jsonCmd.enableJsonOutput(opts.JSON, os.Stdout)
			if opts.JSON {
				// disable output on stdout other than JSON
				log.SetLevel(logging.LogLevelError)
			}
		}

		if logCmd, ok := cmd.(cmdLogger); ok {
			logCmd.setLog(log)
		}

		ctlCfg, err := control.LoadConfig(opts.ConfigPath)
		if err != nil {
			if opts.ConfigPath != "" {
				return errors.WithMessage(err, "failed to load control configuration")
			}
			ctlCfg = control.DefaultConfig()
		}
		if opts.ConfigPath != "" {
			log.Debugf("control config loaded from %s", opts.ConfigPath)
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
			if opts.HostList != "" {
				if hlCmd, ok := cmd.(hostListSetter); ok {
					hlCmd.setHostList(strings.Split(opts.HostList, ","))
				} else {
					return errors.Errorf("this command does not accept a hostlist parameter (set it in %s or %s)",
						control.UserConfigPath(), control.SystemConfigPath())
				}
			}
		}

		if cfgCmd, ok := cmd.(cmdConfigSetter); ok {
			cfgCmd.setConfig(ctlCfg)
		}

		if err := cmd.Execute(args); err != nil {
			return err
		}

		return nil
	}

	_, err := p.ParseArgs(args)
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
