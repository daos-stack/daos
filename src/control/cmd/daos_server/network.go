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

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

type networkCmd struct {
	Scan     networkScanCmd     `command:"scan" description:"Scan for network interface devices on local server"`
	List     networkListCmd     `command:"list" description:"List all known OFI providers that are understood by 'scan'"`
}

// ScanNetCmd is the struct representing the command to scan the machine for network interface devices
// that match the given fabric provider.
type networkScanCmd struct {
	cfgCmd
	logCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider (default is the provider specified in daos_server.yml)"`
	AllProviders bool `short:"a" long:"all" description:"Specify 'all' to see all devices on all providers.  Overrides --provider"`
}

func (cmd *networkScanCmd) Execute(args []string) error {
	var provider string

	if len(args) > 0 {
		cmd.log.Infof("An invalid argument was provided: %v", args)
		return errors.WithMessage(nil, "failed to execute the fabric and device scan.  An invalid argument was provided.")
	}

	if cmd.AllProviders {
		cmd.log.Info("Scanning fabric for all providers")
	} else if len(cmd.FabricProvider) > 0 {
		provider = cmd.FabricProvider
		cmd.log.Infof("Scanning fabric for cmdline specified provider: %s", provider)
	} else if len(cmd.config.Fabric.Provider) > 0 {
		provider = cmd.config.Fabric.Provider
		cmd.log.Infof("Scanning fabric for YML specified provider: %s", provider)
	} else {
		cmd.log.Info("Scanning fabric for all providers")
	}

	results, err := netdetect.ScanFabric(provider)
	if err != nil {
		cmd.log.Error("An error occured while attempting to scan the fabric for devices")
		return errors.WithMessage(err, "failed to execute the fabric and device scan")
	}

	if provider == "" {
		provider = "All"
	}
	cmd.log.Infof("Fabric scan found %d devices matching the provider spec: %s", len(results), provider)

	cmd.log.Debugf("Compressed: %v\n", results)

	for _, sr := range(results) {
		cmd.log.Infof("\n%s\n\n", sr.String())
	}

	return nil
}

type networkListCmd struct {
	cfgCmd
	logCmd
}

func (cmd *networkListCmd) Execute(args []string) error {
	cmd.log.Info("Supported providers:")
	cmd.log.Info("\tofi+gni")
	cmd.log.Info("\tofi+psm2")
	cmd.log.Info("\tofi_rxm")
	cmd.log.Info("\tofi+sockets")
	cmd.log.Info("\tofi+verbs")
	cmd.log.Info("\tofi+verbs;ofi_rxm")
	cmd.log.Info("\nExamples:\n\tdaos_server network scan --provider ofi+sockets")
	cmd.log.Info("\tdaos_server network scan --provider ofi_rxm")
	cmd.log.Info("\tdaos_server network scan --provider \"ofi+sockets;ofi+verbs\"")
	cmd.log.Info("\tdaos_server network scan --provider \"ofi+verbs;ofi_rxm\"")

	return nil
}


