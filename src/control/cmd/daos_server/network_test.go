//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

var (
	testTopo = &hardware.Topology{
		NUMANodes: hardware.NodeMap{
			0: hardware.MockNUMANode(0, 6).WithDevices([]*hardware.PCIDevice{
				&hardware.PCIDevice{
					Name:    "test01",
					PCIAddr: *hardware.MockPCIAddress(1),
					Type:    hardware.DeviceTypeOFIDomain,
				},
				&hardware.PCIDevice{
					Name:    "os_test1",
					PCIAddr: *hardware.MockPCIAddress(1),
					Type:    hardware.DeviceTypeNetInterface,
				},
				&hardware.PCIDevice{
					Name:    "os_test2",
					PCIAddr: *hardware.MockPCIAddress(2),
					Type:    hardware.DeviceTypeNetInterface,
				},
			}),
		},
	}
	if1 = &hardware.FabricInterface{
		Name:          "test01",
		NetInterfaces: common.NewStringSet("os_test1"),
		Providers: hardware.NewFabricProviderSet(
			&hardware.FabricProvider{
				Name: "ofi+verbs",
			}),
	}
)

func scanCfgFromFIS(fis *hardware.FabricInterfaceSet) *hardware.FabricScannerConfig {
	return &hardware.FabricScannerConfig{
		TopologyProvider: &hardware.MockTopologyProvider{
			GetTopoReturn: testTopo,
		},
		FabricInterfaceProviders: []hardware.FabricInterfaceProvider{
			&hardware.MockFabricInterfaceProvider{
				GetFabricReturn: fis,
			},
		},
		NetDevClassProvider: &hardware.MockNetDevClassProvider{
			GetNetDevClassReturn: []hardware.MockGetNetDevClassResult{
				{
					NDC: hardware.Infiniband,
				},
			},
		},
	}
}

func mustLocalHostFabricMap(t *testing.T, hf *control.HostFabric) control.HostFabricMap {
	hfm, err := localHostFabricMap(hf)
	if err != nil {
		t.Fatal(err)
	}

	return hfm
}

func genSetHelpers(t *testing.T, log logging.Logger, fis *hardware.FabricInterfaceSet, srvCfg *config.Server) func(*mainOpts) {
	scanner, err := hardware.NewFabricScanner(log, scanCfgFromFIS(fis))
	if err != nil {
		t.Fatal(err)
	}

	mockNetInit := func(cmd *networkScanCmd) (fabricScanFn, *config.Server, error) {
		return scanner.Scan, srvCfg, nil
	}
	return func(opts *mainOpts) {
		opts.netInitHelper = mockNetInit
	}
}

// TestDaosServer_Network_Commands_JSON verifies that when the JSON-output flag is set only JSON is
// printed to standard out. Test cases should cover all network subcommand variations.
func TestDaosServer_Network_Commands_JSON(t *testing.T) {
	// Use a normal logger to verify that we don't mess up JSON output.
	log, buf := logging.NewTestCommandLineLogger()

	runJSONCmdTests(t, log, buf, []jsonCmdTest{
		{
			"Scan network; nothing found",
			"network scan -j",
			genSetHelpers(t, log, hardware.NewFabricInterfaceSet(), nil),
			nil,
			errors.New("get local fabric interfaces: no fabric interfaces could be found"),
		},
		{
			"Scan network; interface found",
			"network scan -j",
			genSetHelpers(t, log, hardware.NewFabricInterfaceSet(if1), nil),
			mustLocalHostFabricMap(t, &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					{
						Provider:    "ofi+verbs",
						Device:      "os_test1",
						NetDevClass: hardware.Infiniband,
					},
				},
				Providers: []string{"ofi+verbs"},
			}),
			nil,
		},
		{
			"Scan network; config missing",
			"network scan -j -o /really/unusual/directory/location/non-existent.yml",
			genSetHelpers(t, log, hardware.NewFabricInterfaceSet(if1), nil),
			nil,
			errors.New("failed to load config from /really/unusual/directory/location/non-existent.yml: stat /really/unusual/directory/location/non-existent.yml: no such file or directory"),
		},
		{
			"Scan network; nothing matching cli provider found",
			"network scan -j -p ofi+tcp",
			genSetHelpers(t, log, hardware.NewFabricInterfaceSet(if1), nil),
			mustLocalHostFabricMap(t, &control.HostFabric{}),
			nil,
		},
		{
			"Scan network; nothing matching config provider found",
			"network scan -j",
			genSetHelpers(t, log, hardware.NewFabricInterfaceSet(if1),
				config.DefaultServer().WithFabricProvider("ofi+tcp")),
			mustLocalHostFabricMap(t, &control.HostFabric{}),
			nil,
		},
	})
}

// Verify that when --ignore-config is supplied on commandline, cmd.config is nil.
func TestDaosServer_Network_Commands_Config(t *testing.T) {
	for name, tc := range map[string]struct {
		cmd       string
		optsCheck func(t *testing.T, o *mainOpts)
		expErr    error
	}{
		"scan; ignore config": {
			cmd: "network scan --ignore-config",
			optsCheck: func(t *testing.T, o *mainOpts) {
				checkCfgIgnored(t, o.Network.Scan.baseScanCmd)
			},
		},
		"scan; read config": {
			cmd: "network scan",
			optsCheck: func(t *testing.T, o *mainOpts) {
				checkCfgNotIgnored(t, o.Network.Scan.baseScanCmd)
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var opts mainOpts
			genSetHelpers(t, log, hardware.NewFabricInterfaceSet(), nil)(&opts)
			_ = parseOpts(strings.Split(tc.cmd, " "), &opts, log)

			tc.optsCheck(t, &opts)
		})
	}
}
