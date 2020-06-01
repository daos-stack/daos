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
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/control"
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
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider or 'all' for all available (default is the provider specified in daos_server.yml)"`
}

func (cmd *networkScanCmd) Execute(args []string) error {
	var provider string

	if len(args) > 0 {
		return errors.WithMessage(nil, "failed to execute the fabric and device scan.  An invalid argument was provided.")
	}

	provider = cmd.FabricProvider
	switch provider {
	case "":
		provider = cmd.config.Fabric.Provider
	case "all":
		provider = ""
	default:
	}

	results, err := netdetect.ScanFabric(provider)
	if err != nil {
		return errors.WithMessage(err, "failed to execute the fabric and device scan")
	}

	interfaces := []*ctlpb.FabricInterface{}
	for _, fi := range results {
		interfaces = append(interfaces, &ctlpb.FabricInterface{
			Provider: fi.Provider,
			Device:   fi.DeviceName,
			Numanode: uint32(fi.NUMANode),
		})
	}

	nsr := new(control.NetworkScanResp)
	hr := control.HostResponse{
		Addr:    "localhost",
		Message: &ctlpb.NetworkScanResp{Interfaces: interfaces},
	}
	nsr.AddHostResponse(&hr)

	var bld strings.Builder
	if err := pretty.PrintHostFabricMap(nsr.HostFabrics, &bld, false); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return nil
}

type networkListCmd struct {
	cfgCmd
	logCmd
}

// List the available providers
func (cmd *networkListCmd) Execute(args []string) error {
	results, err := netdetect.ScanFabric("")
	if err != nil {
		return errors.WithMessage(err, "failed to execute the fabric and device scan")
	}

	interfaces := []*ctlpb.FabricInterface{}
	for _, fi := range results {
		interfaces = append(interfaces, &ctlpb.FabricInterface{
			Provider: fi.Provider,
			Device:   fi.DeviceName,
			Numanode: uint32(fi.NUMANode),
		})
	}

	nsr := new(control.NetworkScanResp)
	hr := control.HostResponse{
		Addr:    "localhost",
		Message: &ctlpb.NetworkScanResp{Interfaces: interfaces},
	}
	nsr.AddHostResponse(&hr)

	var bld strings.Builder
	if err := pretty.PrintHostFabricMap(nsr.HostFabrics, &bld, true); err != nil {
		return err
	}
	cmd.log.Info(bld.String())

	return nil
}
