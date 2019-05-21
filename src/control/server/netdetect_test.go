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
	"testing"
	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
)

func TestParseTopology(t *testing.T) {
	tests := []struct {
		netDevsList	string
		topology	string
		expected	string
	}{
		{"", "", ""},
		// looks for one specific adatper in a bad topology
		{"eth0", "", ""},
		// looks for one specific adapter in a good topology
		{"ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			"ib0:0:0"},
		// looks for one specific adapter in a good topology
		{"eth0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			"eth0:0:0"},
		// looks for one specific adapter in a good topology
		{"eth1",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			"eth1:0:0"},
		// looks for three adapters in a good topology
		{"eth0,eth1,ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			"eth0:0:0,eth1:0:0,ib0:0:0"},
		// looks for three adaptors, where "ib1" isn't in the topology
			{"eth0,eth1,ib1",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			"eth0:0:0,eth1:0:0"},
		// Test malformed adapter list
		{"eth0:eth1:ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			""},
		// Test malformed adapter list
		{"eth0 eth1 ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			""},
		// look for adaptor that doesn't show up in topology
		{"lo",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			""},
		// Verifies that device is found on the correct NUMA node
		{"eth0",
			"Machine (191GB total) NUMANode L#1 (P#0 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1",
			"eth0:1:0"},
		// Verifies that device is found on correct NUMA node
		{"eth0",
			"Machine (191GB total) NUMANode L#1 (P#1 95GB) " +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#2 (P#1 96GB) + Package L#1",
			"eth0:1:1"},
		// Verifies devices are found on correct NUMA nodes
		{"eth0,eth1,ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"NUMANode L#1 (P#1 96GB) + Package L#1" +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#2 (P#2 96GB) + Package L#2",
			"eth0:1:1,eth1:1:1,ib0:1:1"},
		// Verifies devices are found on correct NUMA nodes
		{"eth0,eth1,ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"NUMANode L#0 (P#0 96GB) + Package L#0" +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"NUMANode L#1 (P#1 96GB) + Package L#1" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"",
			"eth0:0:0,ib0:0:0,eth1:1:1"},
		// Verifies devices are found on correct NUMA nodes
		{"eth0,eth1,ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"NUMANode L#0 (P#1 96GB) + Package L#0" +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"NUMANode L#1 (P#2 96GB) + Package L#1" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"",
			"eth0:0:1,ib0:0:1,eth1:1:2"},
		// Verifies devices are found on correct NUMA nodes
		{"eth0,eth1,ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"NUMANode L#23 (P#16 96GB) + Package L#0" +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"NUMANode L#24 (P#5 96GB) + Package L#1" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"",
			"eth0:23:16,ib0:23:16,eth1:24:5"},
		// Verifies that devices are found on the correct NUMA node
		// with a topology of 3 nodes
		{"eth0,eth1,ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"NUMANode L#23 (P#16 96GB) + Package L#0" +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"NUMANode L#24 (P#5 96GB) + Package L#1" +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"NUMANode L#25 (P#7 96GB) + Package L#1" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"",
			"ib0:23:16,eth0:24:5,eth1:25:7"},
		// Verifies that devices are found on the correct NUMA node
		// with a topology of 5 nodes (more nodes than devices)
		{"eth0,eth1,ib0",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"NUMANode L#23 (P#16 96GB) + Package L#0" +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"NUMANode L#24 (P#5 96GB) + Package L#1" +
			"NUMANode L#28 (P#8 96GB) + Package L#1" +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"NUMANode L#30 (P#7 96GB) + Package L#1" +
			"NUMANode L#32 (P#5 96GB) + Package L#1" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"",
			"ib0:23:16,eth0:28:8,eth1:32:5"},
		// Verifies that 5 devices are found on the correct NUMA node
		// with a topology of 5 nodes (equal number of nodes and devices)
		{"eth0,eth1,ib0,ib1,eth2",
			"Machine (191GB total) NUMANode L#0 (P#0 95GB) " +
			"NUMANode L#23 (P#16 96GB) + Package L#0" +
			"Package L#0 HostBridge L#0 PCI 8086:a1d2 Block(Disk) L#0 " +
			"\"sda\" PCI 8086:a182 PCIBridge PCIBridge PCI 1a03:2000 " +
			"GPU L#1 \"card0\" GPU L#2 \"controlD64\" HostBridge L#3 " +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib0\" OpenFabrics L#4 " +
			"NUMANode L#24 (P#5 96GB) + Package L#1" +
			"PCI 8086:37d2 Net L#7 \"eth1\" OpenFabrics L#8 \"i40iw0\"" +
			"NUMANode L#28 (P#8 96GB) + Package L#1" +
			"\"hfi1_0\" HostBridge L#5 PCIBridge PCIBridge PCIBridge " +
			"PCI 8086:37d2 Net L#5 \"eth0\" OpenFabrics L#6 \"i40iw1\"" +
			"NUMANode L#30 (P#7 96GB) + Package L#1" +
			" PCIBridge PCI 8086:24f0 Net L#3 \"ib1\" OpenFabrics L#4 " +
			"NUMANode L#32 (P#5 96GB) + Package L#1" +
			"PCI 8086:37d2 Net L#7 \"eth2\" OpenFabrics L#8 \"i40iw0\"",
			"ib0:23:16,eth1:24:5,eth0:28:8,ib1:30:7,eth2:32:5"},
		}

	for _, tt := range tests {
		netAdapterAffinity, err := DetectNUMANodeForDevices(tt.netDevsList, tt.topology)
		if err != nil {
			log.Debugf("error was not nil... %v", err)
		}
		AssertEqual(t, netAdapterAffinity, tt.expected, "unexected device found")
	}
}