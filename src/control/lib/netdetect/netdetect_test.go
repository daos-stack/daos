//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package netdetect

import (
	"context"
	"os"
	"testing"

	. "github.com/daos-stack/daos/src/control/common/test"
)

// TestParseTopology uses XML topology data to simulate real systems.
// hwloc will use this topology for queries instead of the local system
// running the test.
func TestParseTopology(t *testing.T) {
	for name, tc := range map[string]struct {
		netDev   string
		topology string
		expected DeviceAffinity
	}{
		"eth0 device affinity (boro 84 system topology)": {
			netDev:   "eth0",
			topology: "testdata/boro-84.xml",
			expected: DeviceAffinity{
				DeviceName: "eth0",
				CPUSet:     "0x000000ff,0xffff0000,0x00ffffff",
				NodeSet:    "0x00000001",
				NUMANode:   0,
			},
		},
		"eth1 device affinity (boro 84 system topology)": {
			netDev:   "eth1",
			topology: "testdata/boro-84.xml",
			expected: DeviceAffinity{
				DeviceName: "eth1",
				CPUSet:     "0x000000ff,0xffff0000,0x00ffffff",
				NodeSet:    "0x00000001",
				NUMANode:   0,
			},
		},
		"ib0 device affinity (boro 84 system topology)": {
			netDev:   "ib0",
			topology: "testdata/boro-84.xml",
			expected: DeviceAffinity{
				DeviceName: "ib0",
				CPUSet:     "0x000000ff,0xffff0000,0x00ffffff",
				NodeSet:    "0x00000001",
				NUMANode:   0,
			},
		},
		"eth0 device affinity (wolf 133 system topology)": {
			netDev:   "eth0",
			topology: "testdata/wolf-133.xml",
			expected: DeviceAffinity{
				DeviceName: "eth0",
				CPUSet:     "0x000000ff,0xffff0000,0x00ffffff",
				NodeSet:    "0x00000001",
				NUMANode:   0,
			},
		},
		"eth1 device affinity (wolf 133 system topology)": {
			netDev:   "eth1",
			topology: "testdata/wolf-133.xml",
			expected: DeviceAffinity{
				DeviceName: "eth1",
				CPUSet:     "0x000000ff,0xffff0000,0x00ffffff",
				NodeSet:    "0x00000001",
				NUMANode:   0,
			},
		},
		"ib0 device affinity (wolf 133 system topology)": {
			netDev:   "ib0",
			topology: "testdata/wolf-133.xml",
			expected: DeviceAffinity{
				DeviceName: "ib0",
				CPUSet:     "0x000000ff,0xffff0000,0x00ffffff",
				NodeSet:    "0x00000001",
				NUMANode:   0,
			},
		},
		"ib1 device affinity (wolf 133 system topology)": {
			netDev:   "ib1",
			topology: "testdata/wolf-133.xml",
			expected: DeviceAffinity{
				DeviceName: "ib1",
				CPUSet:     "0xffffff00,0x0000ffff,0xff000000",
				NodeSet:    "0x00000002",
				NUMANode:   1,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")

			netCtx, err := Init(context.Background())
			defer CleanUp(netCtx)
			AssertEqual(t, err, nil, "Failed to initialize NetDetectContext")

			ndc, err := getContext(netCtx)
			AssertEqual(t, err, nil, "Failed to retrieve context")

			ndc.deviceScanCfg.systemDeviceNames = []string{tc.netDev}
			ndc.deviceScanCfg.systemDeviceNamesMap = make(map[string]struct{})
			for _, deviceName := range ndc.deviceScanCfg.systemDeviceNames {
				ndc.deviceScanCfg.systemDeviceNamesMap[deviceName] = struct{}{}
			}

			ndc.deviceScanCfg.targetDevice = tc.netDev
			AssertEqual(t, err, nil, "Failed to initDeviceScan")

			deviceAffinity, err := GetAffinityForDevice(ndc.deviceScanCfg)
			AssertEqual(t, err, nil, "Failed to GetAffinityForDevice")

			AssertEqual(t, deviceAffinity.DeviceName, tc.expected.DeviceName,
				"unexpected device name mismatch with device and topology")
			AssertEqual(t, deviceAffinity.CPUSet, tc.expected.CPUSet,
				"unexpected CPU set mismatch with device and topology")
			AssertEqual(t, deviceAffinity.NodeSet, tc.expected.NodeSet,
				"unexpected Node set mismatch with device and topology")
			AssertEqual(t, deviceAffinity.NUMANode, tc.expected.NUMANode,
				"unexpected NUMA Node mismatch with device and topology")
		})
	}
}

// TestNumaAware verifies that the numa detection successfully executes without error
// with a standard topology and one without any OS devices in it
func TestNumaAware(t *testing.T) {
	for name, tc := range map[string]struct {
		topology string
	}{
		"no devices in topology": {
			topology: "testdata/gcp_topology.xml",
		},
		"devices in topology": {
			topology: "testdata/boro-84.xml",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")

			netCtx, err := Init(context.Background())
			defer CleanUp(netCtx)
			AssertEqual(t, err, nil, "Failed to initialize NetDetectContext")

			AssertTrue(t, HasNUMA(netCtx), "Unable to detect NUMA on provided topology")
		})
	}
}

// TestInitDeviceScan verifies that the initialization succeeds on NUMA and non NUMA topology
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
			netCtx, err := Init(context.Background())
			defer CleanUp(netCtx)
			AssertEqual(t, err, nil, "Failed to initialize NetDetectContext")
		})
	}
}

// TestGetAffinityForDeviceEdgeCases verifies that determining device affinity
// gives expected output and generates no errors on non NUMA topologies.
func TestGetAffinityForDeviceEdgeCases(t *testing.T) {
	for name, tc := range map[string]struct {
		topology string
		device   string
	}{
		"non-numa topology, with OS devices, no network devices, known input device": {
			device:   "lo",
			topology: "testdata/no-numa-nodes.xml",
		},
		"non-numa topology, with no OS devices, known input device": {
			device:   "lo",
			topology: "testdata/no-numa-no-devices.xml",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := os.Stat(tc.topology)
			AssertEqual(t, err, nil, "unable to load xmlTopology")
			os.Setenv("HWLOC_XMLFILE", tc.topology)
			defer os.Unsetenv("HWLOC_XMLFILE")

			netCtx, err := Init(context.Background())
			defer CleanUp(netCtx)
			AssertEqual(t, err, nil, "Failed to initialize NetDetectContext")

			ndc, err := getContext(netCtx)
			AssertEqual(t, err, nil, "Failed to retrieve context")

			ndc.deviceScanCfg.targetDevice = tc.device
			deviceAffinity, err := GetAffinityForDevice(ndc.deviceScanCfg)

			AssertEqual(t, err, nil, "Unexpected error on GetAffinityForDevice")
			AssertEqual(t, deviceAffinity.NUMANode, uint(0), "deviceAffinity mismatch on NUMA node")
			AssertEqual(t, deviceAffinity.DeviceName, tc.device, "deviceAffinity mismatch on device name")
		})
	}
}
