//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
)

func mockCSFromFabricCfg(t *testing.T, log logging.Logger, config *hardware.FabricScannerConfig, cacheTopology *hardware.Topology) *server.ControlService {
	t.Helper()

	scanner, err := hardware.NewFabricScanner(log, config)
	if err != nil {
		t.Fatal(err)
	}

	return server.NewControlService(log, nil, nil, nil, scanner)
}

func mustLocalHostFabricMap(t *testing.T, hf *control.HostFabric) control.HostFabricMap {
	hfm, err := localHostFabricMap(hf)
	if err != nil {
		t.Fatal(err)
	}

	return hfm
}

// TestDaosServer_Network_Commands_JSON verifies that when the JSON-output flag is set only JSON is
// printed to standard out. Test cases should cover all network subcommand variations.
func TestDaosServer_Network_Commands_JSON(t *testing.T) {
	// Use a normal logger to verify that we don't mess up JSON output.
	log := logging.NewCommandLineLogger()

	testTopo := &hardware.Topology{
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
	if1 := &hardware.FabricInterface{
		Name:          "test01",
		NetInterfaces: common.NewStringSet("os_test1"),
		Providers: hardware.NewFabricProviderSet(
			&hardware.FabricProvider{
				Name: "ofi+verbs",
			}),
	}
	mockDeps := func(fis *hardware.FabricInterfaceSet) *commandDependencies {
		return &commandDependencies{
			ctlSvc: mockCSFromFabricCfg(t, log, &hardware.FabricScannerConfig{
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
			}, nil),
		}
	}

	runJSONCmdTests(t, log, []jsonCmdTest{
		{
			"Scan network; JSON; nothing found",
			"network scan -j",
			mockDeps(hardware.NewFabricInterfaceSet()),
			nil,
			errors.New("get local fabric interfaces: no fabric interfaces could be found"),
		},
		{
			"Scan network; JSON; interface found",
			"network scan -j",
			mockDeps(hardware.NewFabricInterfaceSet(if1)),
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
			"Scan network; JSON; nothing matching provider found",
			"network scan -j -p ofi+tcp",
			mockDeps(hardware.NewFabricInterfaceSet(if1)),
			mustLocalHostFabricMap(t, &control.HostFabric{}),
			nil,
		},
		// TODO DAOS-14529: Test --ignore-config flag
	})
}
