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
	Scan networkScanCmd `command:"scan" description:"Scan for network interface devices on local server"`
	List networkListCmd `command:"list" description:"List all known OFI providers that are understood by 'scan'"`
}

// networkScanCmd is the struct representing the command to scan the machine for network interface devices
// that match the given fabric provider.
type networkScanCmd struct {
	cfgCmd
	logCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider (default is the provider specified in daos_server.yml)"`
	AllProviders   bool   `short:"a" long:"all" description:"Specify 'all' to see all devices on all providers.  Overrides --provider"`
}

func (cmd *networkScanCmd) Execute(args []string) error {
	var provider string

	if len(args) > 0 {
		cmd.log.Debugf("An invalid argument was provided: %+v", args)
		return errors.WithMessage(nil, "failed to execute the fabric and device scan.  An invalid argument was provided.")
	}

	switch {
	case cmd.AllProviders:
		cmd.log.Info("Scanning fabric for all providers")
	case len(cmd.FabricProvider) > 0:
		provider = cmd.FabricProvider
		cmd.log.Infof("Scanning fabric for cmdline specified provider: %s", provider)
	case len(cmd.config.Fabric.Provider) > 0:
		provider = cmd.config.Fabric.Provider
		cmd.log.Infof("Scanning fabric for YML specified provider: %s", provider)
	default:
		// all providers case
		cmd.log.Info("Scanning fabric for all providers")
	}

	results, err := netdetect.ScanFabric(provider)
	if err != nil {
		return errors.WithMessage(err, "failed to execute the fabric and device scan")
	}

	if provider == "" {
		provider = "All"
	}
	cmd.log.Infof("Fabric scan found %d devices matching the provider spec: %s", len(results), provider)

	for _, sr := range results {
		cmd.log.Infof("\n%v\n\n", sr)
	}

	return nil
}

type networkListCmd struct {
	cfgCmd
	logCmd
}

// List the supported providers and show the example text
func (cmd *networkListCmd) Execute(args []string) error {
	providers := netdetect.GetSupportedProviders()
	cmd.log.Info("Supported providers:\n\n")
	for _, p := range providers {
		cmd.log.Infof("\t%s", p)
	}
	return nil
}
