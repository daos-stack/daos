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
					0: MockNUMANode(0, 8).
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
									Type:      DeviceTypeNetInterface,
									PCIAddr:   *MustNewPCIAddress("0000:01:01.1"),
									LinkSpeed: 1.2,
								},
							},
						),
				},
			},
			expOut: `
NUMA Node 0
  CPU cores: 0-7
  PCI devices:
    0000:01:01.1
      0000:01:01.1 test0 (network interface) @ 2.50 GB/s
      0000:01:01.1 test0-peer (network interface) @ 1.20 GB/s
  PCI buses:
    0000:[00-0f]
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
