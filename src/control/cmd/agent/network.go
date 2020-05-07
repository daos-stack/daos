//
// (C) Copyright 2020 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

type netScanCmd struct {
	logCmd
	jsonOutputCmd
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider (default is all providers)"`
}

func (cmd *netScanCmd) Execute(_ []string) error {
	var provider string
	defer os.Exit(0)

	numaAware, err := netdetect.NumaAware()
	if err != nil {
		exitWithError(cmd.log, err)
		return nil
	}

	if !numaAware {
		cmd.log.Info("No NUMA information available.  Any devices found are reported as NUMA node 0.")
	}

	switch {
	case len(cmd.FabricProvider) > 0:
		provider = cmd.FabricProvider
		cmd.log.Infof("Scanning fabric for provider: %s\n", provider)
	default:
		cmd.log.Infof("Scanning fabric for all providers\n")
	}

	results, err := netdetect.ScanFabric(provider)
	if err != nil {
		exitWithError(cmd.log, err)
		return nil
	}

	if provider == "" {
		provider = "All"
	}

	cmd.log.Infof("\nFabric scan found %d devices matching the provider spec: %s\n\n", len(results), provider)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(os.Stdout, results)
	}

	for _, sr := range results {
		cmd.log.Infof("OFI_INTERFACE: %-16sCRT_PHY_ADDR_STR: %-25sNUMA affinity: %d", sr.DeviceName, sr.Provider, sr.NUMANode)
	}

	return nil
}
