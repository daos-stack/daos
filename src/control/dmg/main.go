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
	"strings"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/log"
	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

type cliOptions struct {
	HostList string `short:"l" long:"host-list" description:"comma separated list of addresses <ipv4addr/hostname:port>"`
	// TODO: implement host file parsing
	HostFile   string  `short:"f" long:"host-file" description:"path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList"`
	ConfigPath string  `short:"o" long:"config-path" description:"Client config file path"`
	Storage    StorCmd `command:"storage" alias:"st" description:"Perform tasks related to storage attached to remote servers"`
	Service    SvcCmd  `command:"service" alias:"sv" description:"Perform distributed tasks related to DAOS system"`
	Network    NetCmd  `command:"network" alias:"n" description:"Perform tasks related to network devices attached to remote servers"`
	Pool       PoolCmd `command:"pool" alias:"p" description:"Perform tasks related to DAOS pools"`
}

var (
	opts  = new(cliOptions)
	conns = client.NewConnect()
)

// appSetup loads config file, processes cli overrides and connects clients.
func appSetup(broadcast bool) error {
	config, err := client.ProcessConfigFile(opts.ConfigPath)
	if err != nil {
		return errors.WithMessage(err, "processing config file")
	}

	if opts.HostList != "" {
		config.HostList = strings.Split(opts.HostList, ",")
	}

	if opts.HostFile != "" {
		return errors.New("hostfile option not implemented")
	}

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

func main() {
	if dmgMain() != nil {
		os.Exit(1)
	}
}

func dmgMain() error {
	// Set default global logger for application.
	log.NewDefaultLogger(log.Debug, "", os.Stderr)

	// Parse cli args and either execute subcommand then exit or
	// drop into shell if no subcommand is specified.
	p := flags.NewParser(opts, flags.Default)
	p.SubcommandsOptional = true

	_, err := p.Parse()
	if err != nil {
		return err
	}

	// If no subcommand has been specified, interactive shell is started
	// with expected functionality (tab expansion and utility commands)
	// after parsing config/opts and setting up connections.
	if err := appSetup(true); err != nil {
		fmt.Println(err.Error()) // notify of app setup errors
		fmt.Println("")
	}

	shell := setupShell()
	shell.Println("DAOS Management Shell")
	shell.Run()

	return nil
}
