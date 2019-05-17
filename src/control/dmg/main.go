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
	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/log"
	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
	"os"
	"strings"
)

type cliOptions struct {
	Hostlist string `short:"l" long:"hostlist" description:"comma separated list of addresses <ipv4addr/hostname:port>"`
	// TODO: implement host file parsing
	Hostfile   string  `short:"f" long:"hostfile" description:"path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList"`
	ConfigPath string  `short:"o" long:"config-path" description:"Client config file path"`
	Storage    StorCmd `command:"storage" alias:"st" description:"Perform tasks related to storage attached to remote servers"`
	Service    SvcCmd  `command:"service" alias:"sv" description:"Perform distributed tasks related to DAOS system"`
	Network    NetCmd  `command:"network" alias:"n" description:"Perform tasks related to network devices attached to remote servers"`
	Pool       PoolCmd `command:"pool" alias:"p" description:"Perform tasks related to DAOS pools"`
}

var (
	opts   = new(cliOptions)
	conns  = client.NewConnect()
	config = client.NewConfiguration()
)

func connectHosts() error {

	if opts.Hostfile != "" {
		return errors.New("hostfile option not implemented")
	}

	fmt.Println(sprintConns(conns.ConnectClients(config.HostList)))
	return nil
}

func main() {
	if dmgMain() != nil {
		os.Exit(1)
	}
}

// applyCmdLineOverrides will overwrite Configuration values with any non empty
// data provided, usually from the commandline.
func applyCmdLineOverrides(c *client.Configuration, opts *cliOptions) error {

	if len(opts.Hostlist) > 0 {
		hosts := strings.Split(opts.Hostlist, ",")
		log.Debugf("Overriding hostlist from config file with %s", hosts)
		c.HostList = hosts
	}

	return nil
}

func dmgMain() error {
	// Set default global logger for application.
	log.NewDefaultLogger(log.Debug, "", os.Stderr)

	p := flags.NewParser(opts, flags.Default)
	// Continue with main if no subcommand is executed.
	p.SubcommandsOptional = true

	_, err := p.Parse()
	if err != nil {
		return err
	}

	// Load the configuration file using the supplied path or the default path if none provided.
	config, err = client.ProcessConfigFile(opts.ConfigPath)
	if err != nil {
		log.Errorf("An unrecoverable error occurred while processing the configuration file: %s", err)
		return err
	}

	// Override configuration with any commandline values given
	err = applyCmdLineOverrides(&config, opts)
	if err != nil {
		log.Errorf("Failed to apply command line overrides %s", err)
		return err
	}

	err = connectHosts()
	if err != nil {
		log.Errorf("unable to connect to hosts: %v", err)
		return err
	}

	// If no subcommand is specified, interactive shell is started
	// with expected functionality (tab expansion and utility commands)
	shell := setupShell()
	shell.Println("DAOS Management Shell")
	shell.Run()

	return nil
}
