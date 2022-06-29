//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwloc

import (
	"context"
	"os"
	"path/filepath"
	"runtime"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestHwloc_CacheContext(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, err := CacheContext(context.Background(), log)
	if err != nil {
		t.Fatal(err)
	}

	topo, ok := ctx.Value(topoKey).(*topology)
	if !ok {
		t.Fatalf("key %q not added to context", topoKey)
	}
	if topo == nil {
		t.Fatal("topology was nil")
	}

	cleanup, ok := ctx.Value(cleanupKey).(func())
	if !ok {
		t.Fatalf("key %q not added to context", cleanupKey)
	}
	if cleanup == nil {
		t.Fatal("cleanup function was nil")
	}
	cleanup()
}

func TestHwloc_Cleanup(t *testing.T) {
	cleanupCalled := 0
	mockCleanup := func() {
		cleanupCalled++
	}

	for name, tc := range map[string]struct {
		ctx              context.Context
		expCleanupCalled int
	}{
		"no cleanup cached": {
			ctx: context.Background(),
		},
		"success": {
			ctx:              context.WithValue(context.Background(), cleanupKey, mockCleanup),
			expCleanupCalled: 1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			cleanupCalled = 0

			Cleanup(tc.ctx)
			test.AssertEqual(t, tc.expCleanupCalled, cleanupCalled, "")
		})
	}
}

func hwlocVersion() (major, minor, patch uint) {
	apiVersion := (&api{}).runtimeVersion()
	major = apiVersion >> 16
	minor = (apiVersion & 0xf00) >> 8
	patch = apiVersion & 0x00f

	return
}

func TestHwlocProvider_GetTopology_Samples(t *testing.T) {
	_, filename, _, _ := runtime.Caller(0)
	testdataDir := filepath.Join(filepath.Dir(filename), "testdata")

	// sample hwloc topologies taken from real systems
	for name, tc := range map[string]struct {
		hwlocXMLFile string
		expResult    *hardware.Topology
	}{
		"tds-0002": {
			hwlocXMLFile: filepath.Join(testdataDir, "tds-0002.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 26).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 2),
								hardware.NewPCIBus(0, 3, 5),
								hardware.NewPCIBus(0, 9, 0xb),
								hardware.NewPCIBus(0, 0x7c, 0x7c),
								hardware.NewPCIBus(0, 0x7d, 0x7d),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "sdb",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:11.5"),
								},
								{
									Name:    "sda",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:11.5"),
								},
								{
									Name:      "ens259f0",
									Type:      hardware.DeviceTypeNetInterface,
									PCIAddr:   *hardware.MustNewPCIAddress("0000:04:00.0"),
									LinkSpeed: 7.876923084259033,
								},
								{
									Name:      "ens259f1",
									Type:      hardware.DeviceTypeNetInterface,
									PCIAddr:   *hardware.MustNewPCIAddress("0000:04:00.1"),
									LinkSpeed: 7.876923084259033,
								},
								{
									Name:      "hsn0",
									Type:      hardware.DeviceTypeNetInterface,
									PCIAddr:   *hardware.MustNewPCIAddress("0000:0a:00.0"),
									LinkSpeed: 31.507692337036133,
								},
								{
									Name:    "nvme0n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:7c:00.5"),
								},
								{
									Name:    "nvme3n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:7c:00.5"),
								},
								{
									Name:    "nvme2n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:7c:00.5"),
								},
								{
									Name:    "nvme1n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:7c:00.5"),
								},
								{
									Name:    "nvme6n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:7d:00.5"),
								},
								{
									Name:    "nvme5n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:7d:00.5"),
								},
								{
									Name:    "nvme4n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:7d:00.5"),
								},
								{
									Name:    "nvme7n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:7d:00.5"),
								},
							},
						).
						WithBlockDevices(
							[]*hardware.BlockDevice{
								{
									Name:          "sdb",
									Type:          "Disk",
									Size:          468851544,
									SectorSize:    512,
									LinuxDeviceID: "8:16",
									Model:         "LVM PV mak7fx-KZd1-BqaJ-VODO-GudX-kbdj-h86bq6 on /dev/sdb",
									Revision:      "XC311132",
									SerialNumber:  "BTYH10910CEU480K",
								},
								{
									Name:          "sda",
									Type:          "Disk",
									Size:          468851544,
									SectorSize:    512,
									LinuxDeviceID: "8:0",
									Model:         "LVM PV 0IfIxC-S3jO-PW2h-h1a2-5dT8-T2y7-AsiZi5 on /dev/sda",
									Revision:      "XC311132",
									SerialNumber:  "BTYH10930Y18480K",
								},
								{
									Name:          "nvme0n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:1",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R511648",
								},
								{
									Name:          "nvme3n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:8",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R510280",
								},
								{
									Name:          "nvme2n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:4",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R511669",
								},
								{
									Name:          "nvme1n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:0",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R510279",
								},
								{
									Name:          "nvme6n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:7",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R516328",
								},
								{
									Name:          "nvme5n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:6",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R516079",
								},
								{
									Name:          "nvme4n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:9",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R516306",
								},
								{
									Name:          "nvme7n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:10",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R516341",
								},
								{
									Name:          "pmem0",
									Type:          "NVDIMM",
									Size:          4186568704,
									SectorSize:    512,
									LinuxDeviceID: "259:16",
								},
							},
						),
					1: hardware.MockNUMANode(1, 26, 26).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0x81, 0x81),
								hardware.NewPCIBus(0, 0x82, 0x84),
								hardware.NewPCIBus(0, 0xfa, 0xfa),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:      "hsn1",
									Type:      hardware.DeviceTypeNetInterface,
									PCIAddr:   *hardware.MustNewPCIAddress("0000:83:00.0"),
									LinkSpeed: 31.507692337036133,
								},
								{
									Name:    "nvme11n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:81:00.5"),
								},
								{
									Name:    "nvme9n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:81:00.5"),
								},
								{
									Name:    "nvme10n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:81:00.5"),
								},
								{
									Name:    "nvme8n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:81:00.5"),
								},
								{
									Name:    "nvme15n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:fa:00.5"),
								},
								{
									Name:    "nvme14n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:fa:00.5"),
								},
								{
									Name:    "nvme13n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:fa:00.5"),
								},
								{
									Name:    "nvme12n1",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:fa:00.5"),
								},
							},
						).
						WithBlockDevices(
							[]*hardware.BlockDevice{
								{
									Name:          "nvme11n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:11",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R510293",
								},
								{
									Name:          "nvme9n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:13",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R511639",
								},
								{
									Name:          "nvme10n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:5",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R511666",
								},
								{
									Name:          "nvme8n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:12",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R510298",
								},
								{
									Name:          "nvme15n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:15",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R510256",
								},
								{
									Name:          "nvme14n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:2",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R510311",
								},
								{
									Name:          "nvme13n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:14",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R510281",
								},
								{
									Name:          "nvme12n1",
									Type:          "Disk",
									Size:          15000928256,
									SectorSize:    512,
									LinuxDeviceID: "259:3",
									Vendor:        "Samsung",
									Model:         "SAMSUNG MZWLR15THALA-00007",
									Revision:      "MPK92B5Q",
									SerialNumber:  "S6EXNE0R511647",
								},
								{
									Name:          "pmem1",
									Type:          "NVDIMM",
									Size:          4186568704,
									SectorSize:    512,
									LinuxDeviceID: "259:17",
								},
							},
						),
				},
			},
		},
		"boro-84": {
			hwlocXMLFile: filepath.Join(testdataDir, "boro-84.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 24).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 2),
								hardware.NewPCIBus(0, 0x17, 0x18),
								hardware.NewPCIBus(0, 0x3a, 0x3e),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
								},
								{
									Name:    "hfi1_0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
								},
								{
									Name:    "eth0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.0"),
								},
								{
									Name:    "i40iw1",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.0"),
								},
								{
									Name:    "eth1",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.1"),
								},
								{
									Name:    "i40iw0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.1"),
								},
								{
									Name:    "sda",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:11.5"),
								},
							},
						).
						WithBlockDevices(
							[]*hardware.BlockDevice{
								{
									Name:          "sda",
									Type:          "Disk",
									LinuxDeviceID: "8:0",
									Model:         "INTEL_SSDSCKKI256G8",
									Revision:      "LHF001D",
									SerialNumber:  "BTLA80121CE5256J",
								},
							},
						),
					1: hardware.MockNUMANode(1, 24, 24),
				},
			},
		},
		"wolf-133": {
			hwlocXMLFile: filepath.Join(testdataDir, "wolf-133.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 24).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 2),
								hardware.NewPCIBus(0, 0x17, 0x18),
								hardware.NewPCIBus(0, 0x3a, 0x3e),
								hardware.NewPCIBus(0, 0x5d, 0x5f),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
								},
								{
									Name:    "hfi1_0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
								},
								{
									Name:    "eth0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.0"),
								},
								{
									Name:    "i40iw1",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.0"),
								},
								{
									Name:    "eth1",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.1"),
								},
								{
									Name:    "i40iw0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:3d:00.1"),
								},
								{
									Name:    "sda",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:11.5"),
								},
							},
						).
						WithBlockDevices(
							[]*hardware.BlockDevice{
								{
									Name:          "sda",
									Type:          "Disk",
									LinuxDeviceID: "8:0",
									Model:         "INTEL_SSDSCKKI256G8",
									Revision:      "LHF001D",
									SerialNumber:  "BTLA80230PFN256J",
								},
							},
						),
					1: hardware.MockNUMANode(1, 24, 24).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0xae, 0xaf),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib1",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:af:00.0"),
								},
								{
									Name:    "hfi1_1",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:af:00.0"),
								},
							},
						),
				},
			},
		},
		"no devices": {
			hwlocXMLFile: filepath.Join(testdataDir, "gcp_topology.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 8).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 0),
							},
						),
					1: hardware.MockNUMANode(1, 8, 8),
				},
			},
		},
		"multiport": {
			hwlocXMLFile: filepath.Join(testdataDir, "multiport_hfi_topology.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: hardware.MockNUMANode(0, 8).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0, 7),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:02:00.0"),
								},
								{
									Name:    "enp2s0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:02:00.0"),
								},
								{
									Name:    "mlx4_0",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:02:00.0"),
								},
								{
									Name:    "enp6s0",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:06:00.0"),
								},
								{
									Name:    "sda",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:1f.2"),
								},
								{
									Name:    "sdb",
									Type:    hardware.DeviceTypeBlock,
									PCIAddr: *hardware.MustNewPCIAddress("0000:00:1f.2"),
								},
							},
						).
						WithBlockDevices(
							[]*hardware.BlockDevice{
								{
									Name:          "sda",
									Type:          "Disk",
									LinuxDeviceID: "8:0",
									Model:         "TS64GMTS400",
									Revision:      "N1126I",
									SerialNumber:  "C196500679",
								},
								{
									Name:          "sdb",
									Type:          "Disk",
									LinuxDeviceID: "8:16",
									Model:         "TS64GMTS400",
									Revision:      "N1126I",
									SerialNumber:  "C307450241",
								},
							},
						),
					1: hardware.MockNUMANode(1, 8, 8).
						WithPCIBuses(
							[]*hardware.PCIBus{
								hardware.NewPCIBus(0, 0x80, 0x84),
							},
						).
						WithDevices(
							[]*hardware.PCIDevice{
								{
									Name:    "ib1",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:83:00.0"),
								},
								{
									Name:    "ib2",
									Type:    hardware.DeviceTypeNetInterface,
									PCIAddr: *hardware.MustNewPCIAddress("0000:83:00.0"),
								},
								{
									Name:    "mlx4_1",
									Type:    hardware.DeviceTypeOFIDomain,
									PCIAddr: *hardware.MustNewPCIAddress("0000:83:00.0"),
								},
							},
						),
				},
			},
		},
		"no NUMA nodes in topology": {
			hwlocXMLFile: filepath.Join(testdataDir, "no-numa-nodes.xml"),
			expResult: &hardware.Topology{
				NUMANodes: map[uint]*hardware.NUMANode{
					0: func() *hardware.NUMANode {
						node := hardware.MockNUMANode(0, 4).
							WithPCIBuses(
								[]*hardware.PCIBus{
									hardware.NewPCIBus(0, 0, 3),
									hardware.NewPCIBus(0, 0x17, 0x18),
								},
							).
							WithDevices(
								[]*hardware.PCIDevice{
									{
										Name:    "ib0",
										Type:    hardware.DeviceTypeNetInterface,
										PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
									},
									{
										Name:    "hfi1_0",
										Type:    hardware.DeviceTypeOFIDomain,
										PCIAddr: *hardware.MustNewPCIAddress("0000:18:00.0"),
									},
									{
										Name:    "sda",
										Type:    hardware.DeviceTypeBlock,
										PCIAddr: *hardware.MustNewPCIAddress("0000:00:1f.2"),
									},
									{
										Name:    "sr0",
										Type:    hardware.DeviceTypeBlock,
										PCIAddr: *hardware.MustNewPCIAddress("0000:00:1f.2"),
									},
								},
							).
							WithBlockDevices(
								[]*hardware.BlockDevice{
									{
										Name:          "sda",
										Type:          "Disk",
										LinuxDeviceID: "8:0",
										Model:         "CentOS_Linux_7-0_SSD",
										Revision:      "F.ZDSG90",
										SerialNumber:  "RSQDEVQ5RVR45EJJ1V33",
									},
									{
										Name:          "sr0",
										Type:          "Removable Media Device",
										LinuxDeviceID: "11:0",
										Model:         "Virtual_DVD-ROM__1_",
										Revision:      "FWR1",
										SerialNumber:  "-_31415B265",
									},
								},
							)
						return node
					}(),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			_, err := os.Stat(tc.hwlocXMLFile)
			test.AssertEqual(t, err, nil, "unable to read hwloc XML file")
			os.Setenv("HWLOC_XMLFILE", tc.hwlocXMLFile)
			defer os.Unsetenv("HWLOC_XMLFILE")

			provider := NewProvider(log)
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()

			hwlocMajor, _, _ := hwlocVersion()
			result, err := provider.GetTopology(ctx)
			if err != nil {
				t.Skipf("failed to load %s with hwloc %d.x: %s", tc.hwlocXMLFile, hwlocMajor, err)
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreFields(hardware.BlockDevice{}, "BackingDevice"),
				cmpopts.IgnoreFields(hardware.PCIDevice{}, "BlockDevice"),
			}

			if hwlocMajor < 2 {
				// hwloc 1.x doesn't add the secondary type field
				cmpOpts = append(cmpOpts, cmpopts.IgnoreFields(hardware.BlockDevice{}, "Type"))
			}

			if diff := cmp.Diff(tc.expResult, result, cmpOpts...); diff != "" {
				t.Errorf("(-want, +got)\n%s\n", diff)
			}
		})

	}
}

func TestHwloc_Provider_GetNUMANodeForPID_Parallel(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	runTest_GetNUMANodeForPID_Parallel(t, context.Background(), log)
}

func runTest_GetNUMANodeForPID_Parallel(t *testing.T, parent context.Context, log logging.Logger) {
	t.Helper()

	ctx, cancel := context.WithTimeout(parent, 10*time.Second)
	defer cancel()

	p := NewProvider(log)

	doneCh := make(chan error)
	total := 500

	// If we aren't testing with a cached context, reduce the number
	// of goroutines in order to avoid timing out on more complex
	// topologies.
	if _, err := topologyFromContext(ctx); err != nil {
		total = 128
	}

	for i := 0; i < total; i++ {
		go func() {
			_, err := p.GetNUMANodeIDForPID(ctx, int32(os.Getpid()))
			doneCh <- err
		}()
	}

	for i := 0; i < total; i++ {
		err := <-doneCh
		if err != nil && err != hardware.ErrNoNUMANodes {
			t.Fatal(err)
		}
	}
}

func TestHwloc_Provider_GetNUMANodeForPID_Parallel_CachedCtx(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cachedCtx, err := CacheContext(context.Background(), log)
	if err != nil {
		t.Fatal(err)
	}
	defer Cleanup(cachedCtx)

	runTest_GetNUMANodeForPID_Parallel(t, cachedCtx, log)
}

func TestHwloc_Provider_findNUMANodeWithCPUSet(t *testing.T) {
	_, filename, _, _ := runtime.Caller(0)
	hwlocXMLFile := filepath.Join(filepath.Dir(filename), "testdata", "boro-84.xml")

	_, err := os.Stat(hwlocXMLFile)
	test.AssertEqual(t, err, nil, "unable to read hwloc XML file")
	os.Setenv("HWLOC_XMLFILE", hwlocXMLFile)
	defer os.Unsetenv("HWLOC_XMLFILE")

	provider := NewProvider(nil)

	ctx, cancelCtx := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancelCtx()
	testTopo, cleanup, err := provider.getRawTopology(ctx)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()

	// Harvested from the NUMA nodes in our xml file
	numaCPUSet := map[uint]string{
		0: "0x000000ff,0xffff0000,0x00ffffff",
		1: "0xffffff00,0x0000ffff,0xff000000",
	}

	for name, tc := range map[string]struct {
		getCPUIn  func() (*cpuSet, func())
		expResult uint
		expErr    error
	}{
		"first NUMA": {
			getCPUIn: func() (*cpuSet, func()) {
				return mustCPUSetFromString(numaCPUSet[0], testTopo)
			},
			expResult: 0,
		},
		"second NUMA": {
			getCPUIn: func() (*cpuSet, func()) {
				return mustCPUSetFromString(numaCPUSet[1], testTopo)
			},
			expResult: 1,
		},
		"no match": {
			getCPUIn: func() (*cpuSet, func()) {
				return mustCPUSetFromString("0x00000000", testTopo)
			},
			expErr: errors.New("no NUMA node"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			provider.log = log

			cpuIn, cleanup := tc.getCPUIn()
			defer cleanup()

			result, err := provider.findNUMANodeWithCPUSet(testTopo, cpuIn)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "")
		})

	}
}
