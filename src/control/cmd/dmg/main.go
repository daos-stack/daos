//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"path"
	"strings"

	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
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
)

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

type cliOptions struct {
	AllowProxy bool   `long:"allow-proxy" description:"Allow proxy configuration via environment"`
	HostList   string `short:"l" long:"host-list" description:"comma separated list of addresses <ipv4addr/hostname:port>"`
	Insecure   bool   `short:"i" long:"insecure" description:"have dmg attempt to connect without certificates"`
	Debug      bool   `short:"d" long:"debug" description:"enable debug output"`
	JSON       bool   `short:"j" long:"json" description:"Enable JSON output"`
	// TODO: implement host file parsing
	HostFile   string     `short:"f" long:"host-file" description:"path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList"`
	ConfigPath string     `short:"o" long:"config-path" description:"Client config file path"`
	Storage    storageCmd `command:"storage" alias:"st" description:"Perform tasks related to storage attached to remote servers"`
	System     SystemCmd  `command:"system" alias:"sy" description:"Perform distributed tasks related to DAOS system"`
	Network    NetCmd     `command:"network" alias:"n" description:"Perform tasks related to network devices attached to remote servers"`
	Pool       PoolCmd    `command:"pool" alias:"p" description:"Perform tasks related to DAOS pools"`
}

// appSetup loads config file, processes cli overrides and connects clients.
func appSetup(log logging.Logger, opts *cliOptions, conns client.Connect) error {
	config, err := client.GetConfig(log, opts.ConfigPath)
	if err != nil {
		return errors.WithMessage(err, "processing config file")
	}

	if opts.HostList != "" {
		config.HostList, err = flattenHostAddrs(opts.HostList)
		if err != nil {
			return err
		}
	}

	if opts.HostFile != "" {
		return errors.New("hostfile option not implemented")
	}

	if opts.Insecure {
		config.TransportConfig.AllowInsecure = true
	}

	err = config.TransportConfig.PreLoadCertData()
	if err != nil {
		return errors.Wrap(err, "Unable to load Cerificate Data")
	}
	conns.SetTransportConfig(config.TransportConfig)

	active, inactive, err := checkConns(conns.ConnectClients(config.HostList))
	if err != nil {
		return err
	}
	if len(active) == 0 {
		return errors.New("no active connections")
	}

	log.Infof("Active: %s", strings.Join(active, ","))
	log.Debugf("Inctive: %v", inactive)

	return nil
}

func exitWithError(log logging.Logger, err error) {
	log.Errorf("%s: %v", path.Base(os.Args[0]), err)
	os.Exit(1)
}

func parseOpts(args []string, opts *cliOptions, conns client.Connect, log *logging.LeveledLogger) error {
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
		if opts.JSON {
			log.WithJSONOutput()
		}

		if logCmd, ok := cmd.(cmdLogger); ok {
			logCmd.setLog(log)
		}

		if err := appSetup(log, opts, conns); err != nil {
			return err
		}
		if wantsConn, ok := cmd.(connector); ok {
			wantsConn.setConns(conns)
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

	if err := parseOpts(os.Args[1:], &opts, conns, log); err != nil {
		exitWithError(log, err)
	}
}
