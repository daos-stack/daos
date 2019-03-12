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

// ShowStorageCommand is the struct representing the command to list storage.
type ShowStorageCommand struct{}

// Execute is run when ShowStorageCommand activates
func (s *ShowStorageCommand) Execute(args []string) error {
	// TODO: implement configuration file parsing
	if opts.ConfigPath != "" {
		return errors.New("config-path option not implemented")
	}
	if err := connectHosts(); err != nil {
		return errors.Wrap(err, "unable to connect to hosts")
	}
	fmt.Printf(
		checkAndFormat(conns.ListNvme()),
		"NVMe SSD controller and constituent namespace")
	fmt.Printf(checkAndFormat(conns.ListScm()), "SCM module")
	// exit immediately to avoid continuation of main
	os.Exit(0)
	// never reached
	return nil
}

type cliOptions struct {
	Hostlist    string             `short:"l" long:"hostlist" default:"localhost:10001" description:"comma separated list of addresses <ipv4addr/hostname:port>"`
	Hostfile    string             `short:"f" long:"hostfile" description:"path of hostfile specifying list of addresses <ipv4addr/hostname:port>, if specified takes preference over HostList"`
	ConfigPath  string             `short:"o" long:"config-path" description:"Client config file path"`
	ShowStorage ShowStorageCommand `command:"show-storage" alias:"ss" description:"List attached SCM and NVMe storage"`
}

var (
	opts  = new(cliOptions)
	conns = client.NewConnections()
)

func connectHosts() error {
	if opts.Hostfile != "" {
		return errors.New("hostfile option not implemented")
	}
	hosts := strings.Split(opts.Hostlist, ",")
	if len(hosts) < 1 {
		return errors.New("no hosts to connect to")
	}
	fmt.Println(sprintConns(conns.ConnectClients(hosts)))
	return nil
}

func main() {
	var err error
	defer func() {
		status := 0
		if err != nil {
			status = 1
		}
		os.Exit(status)
	}()

	// Set default global logger for application.
	log.NewDefaultLogger(log.Debug, "", os.Stderr)

	p := flags.NewParser(opts, flags.Default)
	// Continue with main if no subcommand is executed.
	p.SubcommandsOptional = true

	_, err = p.Parse()
	if err != nil {
		return
	}

	// TODO: implement configuration file parsing
	if opts.ConfigPath != "" {
		err = errors.New("config-path option not implemented")
		log.Errorf(err.Error())
		return
	}
	err = connectHosts()
	if err != nil {
		log.Errorf("unable to connect to hosts: %v", err)
		return
	}

	// If no subcommand is specified, interactive shell is started
	// with expected functionality (tab expansion and utility commands)
	shell := setupShell()
	shell.Println("DAOS Management Shell")
	shell.Run()
}
