//
// (C) Copyright 2019 Intel Corporation.
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

// NetCmd is the struct representing the top-level network subcommand.
type NetCmd struct {
	Scan networkScanCmd `command:"scan" description:"Scan for network interface devices on remote servers"`
	List networkListCmd `command:"list" description:"List all known OFI providers on remote servers that are understood by 'scan'"`
}

// networkScanCmd is the struct representing the command to scan the machine for network interface devices
// that match the given fabric provider.
type networkScanCmd struct {
	logCmd
	connectedCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider (default is the provider specified in daos_server.yml)"`
	AllProviders   bool   `short:"a" long:"all" description:"Specify 'all' to see all devices on all providers.  Overrides --provider"`
}

func (cmd *networkScanCmd) Execute(args []string) error {
	var provider string

	if len(args) > 0 {
		cmd.log.Debugf("An invalid argument was provided: %+v", args)
		return nil
	}

	switch {
	case cmd.AllProviders:
		cmd.log.Info("Scanning fabric for all providers")
	case len(cmd.FabricProvider) > 0:
		provider = cmd.FabricProvider
		cmd.log.Infof("Scanning fabric for cmdline specified provider: %s", provider)
	default:
		// all providers case
		cmd.log.Info("Scanning fabric for all providers")
	}

	cmd.log.Infof("Network scan results:\n%v\n", cmd.conns.NetworkScanDevices(provider))
	return nil
}

type networkListCmd struct {
	logCmd
	connectedCmd
}

// List the supported providers
func (cmd *networkListCmd) Execute(args []string) error {
	cmd.log.Infof("Supported Providers:\n%s\n", cmd.conns.NetworkListProviders())
	return nil
}
