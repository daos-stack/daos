//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"encoding/json"
	"io"
	"os"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

func mustLocalHostFabricMap(t *testing.T, hf *control.HostFabric) control.HostFabricMap {
	hfm, err := localHostFabricMap(hf)
	if err != nil {
		t.Fatal(err)
	}

	return hfm
}

type jsonCmdTest2 struct {
	name         string
	cmd          string
	applyMocks   func()
	cleanupMocks func()
	expOut       interface{}
	expErr       error
}

func runJSONCmdTests2(t *testing.T, log *logging.LeveledLogger, cmdTests []jsonCmdTest2) {
	t.Helper()

	for _, tc := range cmdTests {
		t.Run(tc.name, func(t *testing.T) {
			t.Helper()

			// Replace os.Stdout so that we can verify the generated output.
			var result bytes.Buffer
			r, w, _ := os.Pipe()
			done := make(chan struct{})
			go func() {
				_, _ = io.Copy(&result, r)
				close(done)
			}()
			stdout := os.Stdout
			defer func() {
				os.Stdout = stdout
			}()
			os.Stdout = w

			tc.applyMocks()
			defer tc.cleanupMocks()

			var opts mainOpts
			test.CmpErr(t, tc.expErr, parseOpts(strings.Split(tc.cmd, " "), &opts, log))

			w.Close()
			<-done

			// Verify only JSON gets printed.
			if !json.Valid(result.Bytes()) {
				t.Fatalf("invalid JSON in response: %s", result.String())
			}

			var sb strings.Builder
			if err := cmdutil.OutputJSON(&sb, tc.expOut, tc.expErr); err != nil {
				if err != tc.expErr {
					t.Fatalf("OutputJSON: %s", err)
				}
			}

			if diff := cmp.Diff(sb.String(), result.String()); diff != "" {
				t.Fatalf("unexpected stdout (-want, +got):\n%s\n", diff)
			}
		})
	}
}

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

// Generate apply and respective cleanup functions to mock behavior.

func genApplyMockFn(t *testing.T, log logging.Logger, fis *hardware.FabricInterfaceSet, srvCfg *config.Server) func() {
	return func() {
		netCmdInit = func() error {
			return nil
		}

		scanner, err := hardware.NewFabricScanner(log, scanCfgFromFIS(fis))
		if err != nil {
			t.Fatal(err)
		}
		getFabricScanner = func(log logging.Logger) fabricScanFn {

			return scanner.Scan
		}

		getConfig = func(_ cfgCmd) *config.Server {
			return srvCfg
		}
	}
}

func genCleanupMockFn(t *testing.T) func() {
	oldNetCmdInit := netCmdInit
	oldGetFabricScanner := getFabricScanner
	oldGetConfig := getConfig
	return func() {
		netCmdInit = oldNetCmdInit
		getFabricScanner = oldGetFabricScanner
		getConfig = oldGetConfig
	}
}

// TestDaosServer_Network_Commands_JSON verifies that when the JSON-output flag is set only JSON is
// printed to standard out. Test cases should cover all network subcommand variations.
func TestDaosServer_Network_Commands_JSON(t *testing.T) {
	// Use a normal logger to verify that we don't mess up JSON output.
	log := logging.NewCommandLineLogger()

	runJSONCmdTests2(t, log, []jsonCmdTest2{
		{
			"Scan network; JSON; nothing found",
			"network scan -j",
			genApplyMockFn(t, log, hardware.NewFabricInterfaceSet(), nil),
			genCleanupMockFn(t),
			nil,
			errors.New("get local fabric interfaces: no fabric interfaces could be found"),
		},
		{
			"Scan network; JSON; interface found",
			"network scan -j",
			genApplyMockFn(t, log, hardware.NewFabricInterfaceSet(if1), nil),
			genCleanupMockFn(t),
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
			genApplyMockFn(t, log, hardware.NewFabricInterfaceSet(if1), nil),
			genCleanupMockFn(t),
			mustLocalHostFabricMap(t, &control.HostFabric{}),
			nil,
		},
		// TODO DAOS-14529: Test --ignore-config flag
	})
}
