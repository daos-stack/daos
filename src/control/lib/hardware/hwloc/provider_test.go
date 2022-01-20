//
// (C) Copyright 2021 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestHwlocProvider_GetTopology_Samples(t *testing.T) {
	_, filename, _, _ := runtime.Caller(0)
	testdataDir := filepath.Join(filepath.Dir(filename), "testdata")

	// sample hwloc topologies taken from real systems
	for name, tc := range map[string]struct {
		hwlocXMLFile string
		expResult    *hardware.Topology
	}{
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
			defer common.ShowBufferOnFailure(t, buf)

			_, err := os.Stat(tc.hwlocXMLFile)
			common.AssertEqual(t, err, nil, "unable to read hwloc XML file")
			os.Setenv("HWLOC_XMLFILE", tc.hwlocXMLFile)
			defer os.Unsetenv("HWLOC_XMLFILE")

			provider := NewProvider(log)
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()

			result, err := provider.GetTopology(ctx)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Errorf("(-want, +got)\n%s\n", diff)
			}
		})

	}
}
