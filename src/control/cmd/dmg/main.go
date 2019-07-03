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
	"fmt"
	"os"
	"path"
	"strings"

	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/log"
)

type dmgErr string

func (de dmgErr) Error() string {
	return string(de)
}

const (
	// use this error type to signal that a
	// subcommand completed without error
	cmdSuccess dmgErr = "completed successfully"
)

type (
	// this interface decorates a command which
	// should be broadcast rather than unicast
	// to a single access point
	broadcaster interface {
		isBroadcast()
	}
	broadcastCmd struct{}
)

// implement the interface
func (broadcastCmd) isBroadcast() {}

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

type cliOptions struct {
	HostList string `short:"l" long:"host-list" description:"comma separated list of addresses <ipv4addr/hostname:port>"`
	Insecure bool   `short:"i" long:"insecure" description:"have dmg attempt to connect without certificates"`
	// TODO: implement host file parsing
	HostFile   string  `short:"f" long:"host-file" description:"path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList"`
	ConfigPath string  `short:"o" long:"config-path" description:"Client config file path"`
	Storage    StorCmd `command:"storage" alias:"st" description:"Perform tasks related to storage attached to remote servers"`
	Service    SvcCmd  `command:"service" alias:"sv" description:"Perform distributed tasks related to DAOS system"`
	Network    NetCmd  `command:"network" alias:"n" description:"Perform tasks related to network devices attached to remote servers"`
	Pool       PoolCmd `command:"pool" alias:"p" description:"Perform tasks related to DAOS pools"`
}

// appSetup loads config file, processes cli overrides and connects clients.
func appSetup(broadcast bool, opts *cliOptions, conns client.Connect) error {
	config, err := client.GetConfig(opts.ConfigPath)
	if err != nil {
		return errors.WithMessage(err, "processing config file")
	}

	if opts.HostList != "" {
		config.HostList = strings.Split(opts.HostList, ",")
	}

	if opts.HostFile != "" {
		return errors.New("hostfile option not implemented")
	}

	if opts.Insecure == true {
		config.TransportConfig.AllowInsecure = true
	}

	err = config.TransportConfig.PreLoadCertData()
	if err != nil {
		return errors.Wrap(err, "Unable to load Cerificate Data")
	}
	conns.SetTransportConfig(config.TransportConfig)

	// broadcast app requests to host list by default
	addresses := config.HostList
	if !broadcast {
		// send app requests to first access point only
		addresses = []string{config.AccessPoints[0]}
	}

	ok, out := hasConns(conns.ConnectClients(addresses))
	if !ok {
		return errors.New(out) // no active connections
	}

	fmt.Println(out)

	return nil
}

func exitWithError(err error) {
	log.Errorf("%s exiting with error: %v", path.Base(os.Args[0]), err)
	os.Exit(1)
}

func parseOpts(args []string, conns client.Connect) (*cliOptions, error) {
	opts := new(cliOptions)

	p := flags.NewParser(opts, flags.Default)
	p.SubcommandsOptional = true
	p.CommandHandler = func(cmd flags.Commander, args []string) error {
		if cmd == nil {
			return nil
		}

		_, shouldBroadcast := cmd.(broadcaster)
		if err := appSetup(shouldBroadcast, opts, conns); err != nil {
			return err
		}
		if wantsConn, ok := cmd.(connector); ok {
			wantsConn.setConns(conns)
		}
		if err := cmd.Execute(args); err != nil {
			return err
		}

		return cmdSuccess
	}

	unparsed, err := p.ParseArgs(args)
	if err != nil {
		return nil, err
	}
	if len(unparsed) > 0 {
		log.Debugf("Unparsed arguments: %v", unparsed)
	}

	return opts, nil
}

func main() {
	// Set default global logger for application.
	// TODO: Configure level/destination via CLI opts
	log.NewDefaultLogger(log.Debug, "", os.Stderr)

	conns := client.NewConnect()

	opts, err := parseOpts(os.Args[1:], conns)
	if err != nil {
		if err == cmdSuccess {
			os.Exit(0)
		}
		exitWithError(err)
	}

	// If no subcommand has been specified, interactive shell is started
	// with expected functionality (tab expansion and utility commands)
	// after parsing config/opts and setting up connections.
	if err := appSetup(true, opts, conns); err != nil {
		fmt.Println(err.Error()) // notify of app setup errors
		fmt.Println("")
	}

	shell := setupShell(conns)
	shell.Println("DAOS Management Shell")
	shell.Run()
}
