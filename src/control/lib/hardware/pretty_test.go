//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"bytes"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestHardware_PrintTopology(t *testing.T) {
	testNUMANode := MockNUMANode(0, 8).
		WithPCIBuses(
			[]*PCIBus{
				{
					LowAddress:  *MustNewPCIAddress("0000:00:00.0"),
					HighAddress: *MustNewPCIAddress("0000:0f:00.0"),
				},
			},
		).
		WithDevices(
			[]*PCIDevice{
				{
					Name:      "test0",
					Type:      DeviceTypeNetInterface,
					PCIAddr:   *MustNewPCIAddress("0000:01:01.1"),
					LinkSpeed: 2.5,
				},
				{
					Name:      "test0-peer",
					Type:      DeviceTypeOFIDomain,
					PCIAddr:   *MustNewPCIAddress("0000:01:01.1"),
					LinkSpeed: 1.2,
				},
			},
		)

	for name, tc := range map[string]struct {
		topo   *Topology
		expOut string
	}{
		"nil": {
			expOut: "No topology information available\n",
		},
		"basic": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: testNUMANode,
				},
			},
			expOut: `
NUMA Node 0
  CPU cores: 0-7
  PCI buses:
    0000:[00-0f]
      0000:01:01.1
        0000:01:01.1 test0 (network interface) @ 2.50 GB/s
        0000:01:01.1 test0-peer (OFI domain) @ 1.20 GB/s
`,
		},
		"virtual": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: testNUMANode,
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
					},
					{
						Name:          "virt1",
						Type:          DeviceTypeNetInterface,
						BackingDevice: testNUMANode.PCIDevices[*MustNewPCIAddress("0000:01:01.1")][0],
					},
				},
			},
			expOut: `
NUMA Node 0
  CPU cores: 0-7
  PCI buses:
    0000:[00-0f]
      0000:01:01.1
        0000:01:01.1 test0 (network interface) @ 2.50 GB/s
        0000:01:01.1 test0-peer (OFI domain) @ 1.20 GB/s
Virtual Devices
  virt0 (network interface)
  virt1 (network interface)
    backed by: 0000:01:01.1 test0 (network interface) @ 2.50 GB/s
`,
		},
		"multiple NUMA nodes": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					2: MockNUMANode(2, 8, 8).
						WithPCIBuses(
							[]*PCIBus{
								{
									LowAddress:  *MustNewPCIAddress("0000:00:00.0"),
									HighAddress: *MustNewPCIAddress("0000:0f:00.0"),
								},
							},
						).
						WithDevices(
							[]*PCIDevice{
								{
									Name:      "test2-net",
									Type:      DeviceTypeNetInterface,
									PCIAddr:   *MustNewPCIAddress("0000:01:01.1"),
									LinkSpeed: 2.5,
								},
								{
									Name:    "test2-block",
									Type:    DeviceTypeBlock,
									PCIAddr: *MustNewPCIAddress("0000:00:1f.2"),
								},
							},
						),
					0: MockNUMANode(0, 8).
						WithPCIBuses(
							[]*PCIBus{
								{
									LowAddress:  *MustNewPCIAddress("0000:80:00.0"),
									HighAddress: *MustNewPCIAddress("0000:88:00.0"),
								},
							},
						).
						WithDevices(
							[]*PCIDevice{
								{
									Name:      "test0-net",
									Type:      DeviceTypeNetInterface,
									PCIAddr:   *MustNewPCIAddress("0000:80:01.1"),
									LinkSpeed: 2.5,
								},
								{
									Name:    "test0-block",
									Type:    DeviceTypeBlock,
									PCIAddr: *MustNewPCIAddress("0000:83:00.0"),
								},
							},
						),
				},
			},
			expOut: `
NUMA Node 0
  CPU cores: 0-7
  PCI buses:
    0000:[80-88]
      0000:80:01.1
        0000:80:01.1 test0-net (network interface) @ 2.50 GB/s
      0000:83:00.0
        0000:83:00.0 test0-block (block device)
NUMA Node 2
  CPU cores: 8-15
  PCI buses:
    0000:[00-0f]
      0000:00:1f.2
        0000:00:1f.2 test2-block (block device)
      0000:01:01.1
        0000:01:01.1 test2-net (network interface) @ 2.50 GB/s
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var out bytes.Buffer
			err := PrintTopology(tc.topo, &out)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(strings.TrimLeft(tc.expOut, "\n"), out.String()); diff != "" {
				t.Fatalf("Unexpected output (-want +got):\n%s", diff)
			}
		})
	}
}
