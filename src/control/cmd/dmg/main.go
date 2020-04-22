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
	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	defaultConfigFile = "daos.yml"
)

type (
	// this interface decorates a command which
	// requires a connection to the control
	// plane and therefore must have an
	// implementation of client.Connect
	connector interface {
		setConns(client.Connect)
	}

	connectedCmd struct {
		conns client.Connect
	}

	ctlClientUser interface {
		setClient(control.Invoker)
		setHostList([]string)
	}

	ctlClientCmd struct {
		hostlist  []string
		ctlClient control.Invoker
	}

	jsonOutputter interface {
		enableJsonOutput(bool)
		jsonOutputEnabled() bool
		outputJSON(io.Writer, interface{}) error
	}

	jsonOutputCmd struct {
		shouldEmitJSON bool
	}
)

func (cmd *ctlClientCmd) setClient(c control.Invoker) {
	cmd.ctlClient = c
}

func (cmd *ctlClientCmd) setHostList(hl []string) {
	cmd.hostlist = hl
}

func (cmd *jsonOutputCmd) enableJsonOutput(emitJson bool) {
	cmd.shouldEmitJSON = emitJson
}

func (cmd *jsonOutputCmd) jsonOutputEnabled() bool {
	return cmd.shouldEmitJSON
}

func (cmd *jsonOutputCmd) outputJSON(out io.Writer, in interface{}) error {
	data, err := json.Marshal(in)
	if err != nil {
		return err
	}

	_, err = out.Write(data)
	return err
}

// implement the interface
func (cmd *connectedCmd) setConns(conns client.Connect) {
	cmd.conns = conns
}

type cmdLogger interface {
	setLog(*logging.LeveledLogger)
}

type logCmd struct {
	log *logging.LeveledLogger
}

func (c *logCmd) setLog(log *logging.LeveledLogger) {
	c.log = log
}

// cmdConfigSetter is an interface for setting the client config on a command
type cmdConfigSetter interface {
	setConfig(*client.Configuration)
}

// cfgCmd is a structure that can be used by commands that need the client
// config.
type cfgCmd struct {
	config *client.Configuration
}

func (c *cfgCmd) setConfig(cfg *client.Configuration) {
	c.config = cfg
}

type cliOptions struct {
	AllowProxy bool   `long:"allow-proxy" description:"Allow proxy configuration via environment"`
	HostList   string `short:"l" long:"host-list" description:"comma separated list of addresses <ipv4addr/hostname:port>"`
	Insecure   bool   `short:"i" long:"insecure" description:"have dmg attempt to connect without certificates"`
	Debug      bool   `short:"d" long:"debug" description:"enable debug output"`
	JSON       bool   `short:"j" long:"json" description:"Enable JSON output"`
	JSONLogs   bool   `short:"J" long:"json-logging" description:"Enable JSON-formatted log output"`
	// TODO: implement host file parsing
	HostFile   string     `short:"f" long:"host-file" description:"path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList"`
	ConfigPath string     `short:"o" long:"config-path" description:"Client config file path"`
	Storage    storageCmd `command:"storage" alias:"st" description:"Perform tasks related to storage attached to remote servers"`
	System     SystemCmd  `command:"system" alias:"sy" description:"Perform distributed tasks related to DAOS system"`
	Network    NetCmd     `command:"network" alias:"n" description:"Perform tasks related to network devices attached to remote servers"`
	Pool       PoolCmd    `command:"pool" alias:"p" description:"Perform tasks related to DAOS pools"`
	Cont       ContCmd    `command:"cont" alias:"c" description:"Perform tasks related to DAOS containers"`
	Version    versionCmd `command:"version" description:"Print dmg version"`
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

func parseOpts(args []string, opts *cliOptions, ctlClient control.Invoker, conns client.Connect, log *logging.LeveledLogger) error {
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
			jsonCmd.enableJsonOutput(opts.JSON)
		}

		if logCmd, ok := cmd.(cmdLogger); ok {
			logCmd.setLog(log)
		}

		if opts.ConfigPath == "" {
			defaultConfigPath := path.Join(build.ConfigDir, defaultConfigFile)
			if _, err := os.Stat(defaultConfigPath); err == nil {
				opts.ConfigPath = defaultConfigPath
			}
		}

		config, err := client.GetConfig(log, opts.ConfigPath)
		if err != nil {
			return errors.WithMessage(err, "processing config file")
		}

		ctlClientCfg, err := control.LoadClientConfig(opts.ConfigPath)
		if err != nil {
			if opts.ConfigPath != "" {
				return errors.WithMessage(err, "failed to load client configuration")
			}
			ctlClientCfg = control.DefaultClientConfig()
		}

		if opts.HostList != "" {
			hostlist, err := flattenHostAddrs(opts.HostList, config.ControlPort)
			if err != nil {
				return err
			}
			config.HostList = hostlist
		}

		if opts.HostFile != "" {
			return errors.New("hostfile option not implemented")
		}

		if opts.Insecure {
			config.TransportConfig.AllowInsecure = true
			ctlClientCfg.TransportConfig.AllowInsecure = true
		}

		if err := ctlClientCfg.TransportConfig.PreLoadCertData(); err != nil {
			return errors.Wrap(err, "Unable to load Certificate Data")
		}

		err = config.TransportConfig.PreLoadCertData()
		if err != nil {
			return errors.Wrap(err, "Unable to load Certificate Data")
		}
		conns.SetTransportConfig(config.TransportConfig)

		if wantsConn, ok := cmd.(connector); ok {
			connStates, err := checkConns(conns.ConnectClients(config.HostList))
			if err != nil {
				return err
			}
			if _, exists := connStates["connected"]; !exists {
				log.Error(connStates.String())
				return errors.New("no active connections")
			}

			log.Info(connStates.String())
			wantsConn.setConns(conns)
		}

		ctlClient.SetClientConfig(ctlClientCfg)
		if clientCmd, ok := cmd.(ctlClientUser); ok {
			clientCmd.setClient(ctlClient)
			if opts.HostList != "" {
				clientCmd.setHostList(strings.Split(opts.HostList, ","))
			}
		}

		if cfgCmd, ok := cmd.(cmdConfigSetter); ok {
			cfgCmd.setConfig(config)
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

	conns := client.NewConnect(log)
	ctlClient := control.NewClient(
		control.WithClientLogger(log),
	)

	if err := parseOpts(os.Args[1:], &opts, ctlClient, conns, log); err != nil {
		exitWithError(log, err)
	}
}
