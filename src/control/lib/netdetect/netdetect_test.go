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

package netdetect

import (
	"fmt"
	"os"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
)

// TestParseTopology uses XML topology data to simulate real systems.
// hwloc will use this topology for queries instead of the local system
// running the test.
func TestParseTopology(t *testing.T) {
	for name, tc := range map[string]struct {
		netDevsList []string
		topology    string
		expected    []string
	}{
		"no device given device affinity (boro 84 system topology)": {
			netDevsList: []string{""},
			topology:    "testdata/boro-84.xml",
			expected:    []string{},
		},
		"eth0 device affinity (boro 84 system topology)": {
			netDevsList: []string{"eth0"},
			topology:    "testdata/boro-84.xml",
			expected:    []string{"eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"eth1 device affinity (boro 84 system topology)": {
			netDevsList: []string{"eth1"},
			topology:    "testdata/boro-84.xml",
			expected:    []string{"eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"ib0 device affinity (boro 84 system topology)": {
			netDevsList: []string{"ib0"},
			topology:    "testdata/boro-84.xml",
			expected:    []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"eth0 eth1 device affinity (boro 84 system topology)": {
			netDevsList: []string{"eth0", "eth1"},
			topology:    "testdata/boro-84.xml",
			expected:    []string{"eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"eth0 eth1 ib0 device affinity (boro 84 system topology)": {
			netDevsList: []string{"eth0", "eth1", "ib0"},
			topology:    "testdata/boro-84.xml",
			expected:    []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"ib0 eth0 ib1 eth1 device affinity (boro 84 system topology)": {
			netDevsList: []string{"ib0", "eth0", "ib1", "eth1"},
			topology:    "testdata/boro-84.xml",
			expected:    []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"eth0 device affinity (wolf 133 system topology)": {
			netDevsList: []string{"eth0"},
			topology:    "testdata/wolf-133.xml",
			expected:    []string{"eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"eth1 device affinity (wolf 133 system topology)": {
			netDevsList: []string{"eth1"},
			topology:    "testdata/wolf-133.xml",
			expected:    []string{"eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"ib0 device affinity (wolf 133 system topology)": {
			netDevsList: []string{"ib0"},
			topology:    "testdata/wolf-133.xml",
			expected:    []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"ib1 device affinity (wolf 133 system topology)": {
			netDevsList: []string{"ib1"},
			topology:    "testdata/wolf-133.xml",
			expected:    []string{"ib1:0xffffff00,0x0000ffff,0xff000000:0x00000002:1"},
		},
		"eth0 eth1 device affinity (wolf-133 system topology)": {
			netDevsList: []string{"eth0", "eth1"},
			topology:    "testdata/wolf-133.xml",
			expected:    []string{"eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"ib0 eth0 eth1 device affinity (wolf 133 system topology)": {
			netDevsList: []string{"ib0", "eth0", "eth1"},
			topology:    "testdata/boro-84.xml",
			expected:    []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0"},
		},
		"ib0 eth0 ib1 eth1 device affinity (wolf 133 system topology)": {
			netDevsList: []string{"ib0", "eth0", "ib1", "eth1"},
			topology:    "testdata/wolf-133.xml",
			expected:    []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001:0", "ib1:0xffffff00,0x0000ffff,0xff000000:0x00000002:1"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")
			netAdapterAffinity, err := GetAffinityForNetworkDevices(tc.netDevsList)
			if err != nil {
				t.Fatal(err)
			}
			AssertEqual(t, len(netAdapterAffinity), len(tc.expected), "number of devices expected vs found does not match")
			for j, i := range netAdapterAffinity {
				AssertEqual(t, i.String(), tc.expected[j],
					"unexpected mismatch with device and topology")
			}
		})
	}

}

// TestScanFabricNoDevices uses a topology with no devices reported and scans the fabric against that.
// The ScanFabric should encounter no errors, and should return an empty scan result.
func TestScanFabricNoDevices(t *testing.T) {
	for name, tc := range map[string]struct {
		results  int
		topology string
		excludes string
	}{
		"no devices in topology": {
			results:  0,
			topology: "testdata/gcp_topology.xml",
			excludes: "lo",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")

			results, err := ScanFabric("", tc.excludes)
			for i, res := range results {
				fmt.Printf("Result %d\n\n%v\n\n", i, res)
			}
			AssertEqual(t, err, nil, "ScanFabric failure")
			AssertEqual(t, len(results), tc.results, "ScanFabric had unexpected number of results")
		})
	}
}

// TestScanFabric scans the fabric on the test system.  Even though we don't know how the test system is configured,
// we do expect that libfabric is installed and will report at least one provider,device,numa record.
// If we get at least one record and no errors, the test is successful.
func TestScanFabric(t *testing.T) {
	results, err := ScanFabric("")
	AssertEqual(t, err, nil, "ScanFabric failure")
	AssertEqual(t, len(results) > 0, true, "ScanFabric had no results")
}

// TestValidateNetworkConfig runs in a basic loopback mode with ScanFabric.  ScanFabric
// is used to generate data found on the actual test system, which is then fed back to the
// ValidateProviderConfig and  ValidateNUMAConfig functions to make sure it matches.  Each record from ScanFabric is
// examined.  We expect that libfabric is installed and will report at least one provider, device, numa record.
func TestValidateNetworkConfig(t *testing.T) {

	provider := "" // an empty provider string is a search for 'all'
	results, err := ScanFabric(provider)
	if err != nil {
		t.Fatal(err)
	}

	if len(results) == 0 {
		t.Fatal(err)
	}

	for _, sf := range results {
		err := ValidateProviderConfig(sf.DeviceName, sf.Provider)
		AssertEqual(t, err, nil, "Network device configuration is invalid - provider not supported")

		err = ValidateNUMAConfig(sf.DeviceName, sf.NUMANode)
		AssertEqual(t, err, nil, "Network device configuration is invalid - NUMA node does not match")
	}
}

// ValidateNUMAConfigNonNumaAware verifies that the numa detection successfully executes without error
// with a topology that has no NUMA devices in it.
func TestValidateNUMAConfigNonNumaAware(t *testing.T) {
	for name, tc := range map[string]struct {
		device   string
		topology string
	}{
		"no NUMA nodes in topology - has devices": {
			device:   "fake device",
			topology: "testdata/no-numa-nodes.xml",
		},
		"no NUMA nodes in topology with eth0 device": {
			device:   "eth0",
			topology: "testdata/no-numa-nodes.xml",
		},
		"no NUMA nodes in topology with ib1 device": {
			device:   "ib1",
			topology: "testdata/no-numa-nodes.xml",
		},
		"no NUMA nodes no devices in topology with fake device": {
			device:   "fake device",
			topology: "testdata/no-numa-no-devices.xml",
		},
		"no NUMA nodes no devices in topology with eth0 device": {
			device:   "eth0",
			topology: "testdata/no-numa-no-devices.xml",
		},
		"no NUMA nodes no devices in topology with fake device with ib1 device": {
			device:   "ib1",
			topology: "testdata/no-numa-no-devices.xml",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")

			numa, err := NumaAware()
			AssertEqual(t, err, nil, "Error on NumaAware")
			AssertEqual(t, numa, false, "Unexpected detection of NUMA nodes in provided topology")

			err = ValidateNUMAConfig(tc.device, 0)
			AssertEqual(t, err, nil, "Error on ValidateNUMAConfig")
		})
	}
}

// TestNumaAware verifies that the numa detection successfully executes without error
// with a standard topology and one without any OS devices in it
func TestNumaAware(t *testing.T) {
	for name, tc := range map[string]struct {
		result   bool
		topology string
	}{
		"no devices in topology": {
			result:   true,
			topology: "testdata/gcp_topology.xml",
		},
		"devices in topology": {
			result:   true,
			topology: "testdata/boro-84.xml",
		},
		"devices in topology no NUMA nodes": {
			result:   false,
			topology: "testdata/no-numa-nodes.xml",
		},
		"no devices in topology no NUMA nodes": {
			result:   false,
			topology: "testdata/no-numa-no-devices.xml",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")

			numa, err := NumaAware()
			AssertEqual(t, err, nil, "Error on NumaAware")
			if tc.result {
				AssertEqual(t, numa, tc.result, "Unable to detect NUMA on provided topology")
			} else {
				AssertEqual(t, numa, tc.result, "Detected NUMA on non-numa topology")
			}
		})
	}
}

// TestInitDeviceScan verifies that the initialization succeeds on numa and non NUMA topology
func TestInitDeviceScan(t *testing.T) {
	for name, tc := range map[string]struct {
		topology string
	}{
		"no devices in topology": {
			topology: "testdata/gcp_topology.xml",
		},
		"devices in topology": {
			topology: "testdata/boro-84.xml",
		},
		"devices in topology no NUMA nodes": {
			topology: "testdata/no-numa-nodes.xml",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")

			_, err = initDeviceScan()
			AssertEqual(t, err, nil, "Error on initDeviceScan")
		})
	}
}

// TestDeviceAliasErrors uses XML topology data to simulate real systems.
// hwloc will use this topology for queries instead of the local system
// running the test.
// This test verifies that getDeviceAliasWithSystemList() handles the error cases when a specified
// device cannot be matched to a sibling.
func TestDeviceAliasErrors(t *testing.T) {
	for name, tc := range map[string]struct {
		device   string
		topology string
	}{
		"eth2 alias lookup (boro 84 system topology)": {
			device:   "eth2",
			topology: "testdata/boro-84.xml",
		},
		"ib2 alias lookup (boro 84 system topology)": {
			device:   "ib2",
			topology: "testdata/boro-84.xml",
		},
		"mlx15 alias lookup (boro 84 system topology)": {
			device:   "mlx15",
			topology: "testdata/boro-84.xml",
		},
		"hfi1_01 alias lookup (wolf-133 system topology)": {
			device:   "hfi1_01",
			topology: "testdata/wolf-133.xml",
		},
		"i40iw02 alias lookup (wolf-133 system topology)": {
			device:   "i40iw02",
			topology: "testdata/wolf-133.xml",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")
			deviceAlias, err := getDeviceAliasWithSystemList(tc.device, []string{})
			AssertTrue(t, err != nil,
				"an error was expected but not received")
			AssertEqual(t, deviceAlias, "",
				"an unexpected sibling match was found in the topology")
		})
	}
}

// TestDeviceAlias uses XML topology data to simulate real systems.
// hwloc will use this topology for queries instead of the local system running the test.
// This test verifies that getDeviceAliasWithSystemList() is capable of performing this lookup
// This particular test verifies the expected lookup path that uses a device found
// on the system device list, and expected to find the related sibling.
func TestDeviceAlias(t *testing.T) {
	mockSystemDevices := []string{"ib0", "ib1", "ib2", "enp2s0", "eth0", "eth1"}
	for name, tc := range map[string]struct {
		device   string
		topology string
		expected string
	}{
		"loopback alias lookup (boro 84 system topology)": {
			device:   "lo",
			topology: "testdata/boro-84.xml",
			expected: "lo",
		},
		"eth0 alias lookup (boro 84 system topology)": {
			device:   "eth0",
			topology: "testdata/boro-84.xml",
			expected: "i40iw1",
		},
		"eth1 alias lookup (boro 84 system topology)": {
			device:   "eth1",
			topology: "testdata/boro-84.xml",
			expected: "i40iw0",
		},
		"ib0 alias lookup (boro 84 system topology)": {
			device:   "ib0",
			topology: "testdata/boro-84.xml",
			expected: "hfi1_0",
		},
		"eth0 alias lookup (wolf-133 system topology)": {
			device:   "eth0",
			topology: "testdata/wolf-133.xml",
			expected: "i40iw1",
		},
		"eth1 alias lookup (wolf-133 system topology)": {
			device:   "eth1",
			topology: "testdata/wolf-133.xml",
			expected: "i40iw0",
		},
		"ib0 alias lookup (wolf-133 system topology)": {
			device:   "ib0",
			topology: "testdata/wolf-133.xml",
			expected: "hfi1_0",
		},
		"ib1 alias lookup (wolf-133 system topology)": {
			device:   "ib1",
			topology: "testdata/wolf-133.xml",
			expected: "hfi1_1",
		},
		"ib0 alias lookup (multiport-hfi system topology)": {
			device:   "ib0",
			topology: "testdata/multiport_hfi_topology.xml",
			expected: "mlx4_0",
		},
		"enp2s0 alias lookup (multiport-hfi system topology)": {
			device:   "enp2s0",
			topology: "testdata/multiport_hfi_topology.xml",
			expected: "mlx4_0",
		},
		"ib1 alias lookup (multiport-hfi system topology": {
			device:   "ib1",
			topology: "testdata/multiport_hfi_topology.xml",
			expected: "mlx4_1",
		},
		"ib2 alias lookup (multiport-hfi system topology)": {
			device:   "ib2",
			topology: "testdata/multiport_hfi_topology.xml",
			expected: "mlx4_1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")
			deviceAlias, err := getDeviceAliasWithSystemList(tc.device, mockSystemDevices)
			if err != nil {
				t.Fatal(err)
			}
			AssertEqual(t, deviceAlias, tc.expected,
				"unexpected mismatch with device and topology")
		})
	}
}

// TestValidateProviderSm verifies that using the shared memory provider 'sm'
// is validated with a positive result.
func TestValidateProviderSm(t *testing.T) {
	provider := "" // an empty provider string is a search for 'all'
	results, err := ScanFabric(provider)
	if err != nil {
		t.Fatal(err)
	}

	if len(results) == 0 {
		t.Fatal(err)
	}

	for _, sf := range results {
		err := ValidateProviderConfig(sf.DeviceName, "sm")
		AssertEqual(t, err, nil, "Network device configuration is invalid - provider not supported")
	}
}
