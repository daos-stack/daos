//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common"
)

func TestHardware_Topology_AllDevices(t *testing.T) {
	for name, tc := range map[string]struct {
		topo      *Topology
		expResult map[string]*PCIDevice
	}{
		"nil": {
			expResult: make(map[string]*PCIDevice),
		},
		"no NUMA nodes": {
			topo:      &Topology{},
			expResult: make(map[string]*PCIDevice),
		},
		"no PCI addrs": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: MockNUMANode(0, 8),
				},
			},
			expResult: make(map[string]*PCIDevice),
		},
		"single device": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: MockNUMANode(0, 8).WithDevices(
						[]*PCIDevice{
							mockPCIDevice("test").withType(DeviceTypeNetInterface),
						},
					),
				},
			},
			expResult: map[string]*PCIDevice{
				"test00": mockPCIDevice("test").withType(DeviceTypeNetInterface),
			},
		},
		"multi device": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: MockNUMANode(0, 8).WithDevices(
						[]*PCIDevice{
							mockPCIDevice("test0", 1, 1, 0).withType(DeviceTypeNetInterface),
							mockPCIDevice("test1", 1, 1, 0).withType(DeviceTypeOFIDomain),
							mockPCIDevice("test2", 1, 2, 1).withType(DeviceTypeNetInterface),
							mockPCIDevice("test3", 1, 2, 1).withType(DeviceTypeUnknown),
						},
					),
					1: MockNUMANode(1, 8).WithDevices(
						[]*PCIDevice{
							mockPCIDevice("test4", 2, 1, 1).withType(DeviceTypeNetInterface),
							mockPCIDevice("test5", 2, 1, 1).withType(DeviceTypeOFIDomain),
							mockPCIDevice("test6", 2, 2, 1).withType(DeviceTypeNetInterface),
						},
					),
				},
			},
			expResult: map[string]*PCIDevice{
				"test001": mockPCIDevice("test0", 1, 1, 0).withType(DeviceTypeNetInterface),
				"test101": mockPCIDevice("test1", 1, 1, 0).withType(DeviceTypeOFIDomain),
				"test201": mockPCIDevice("test2", 1, 2, 1).withType(DeviceTypeNetInterface),
				"test301": mockPCIDevice("test3", 1, 2, 1).withType(DeviceTypeUnknown),
				"test402": mockPCIDevice("test4", 2, 1, 1).withType(DeviceTypeNetInterface),
				"test502": mockPCIDevice("test5", 2, 1, 1).withType(DeviceTypeOFIDomain),
				"test602": mockPCIDevice("test6", 2, 2, 1).withType(DeviceTypeNetInterface),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.topo.AllDevices()

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreFields(PCIDevice{}, "NUMANode"),
			}
			if diff := cmp.Diff(tc.expResult, result, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestTopology_NumNUMANodes(t *testing.T) {
	for name, tc := range map[string]struct {
		topo      *Topology
		expResult int
	}{
		"nil": {},
		"empty": {
			topo: &Topology{},
		},
		"one": {
			topo: &Topology{
				NUMANodes: NodeMap{
					0: MockNUMANode(0, 8),
				},
			},
			expResult: 1,
		},
		"multiple": {
			topo: &Topology{
				NUMANodes: NodeMap{
					0: MockNUMANode(0, 8),
					1: MockNUMANode(1, 8),
					2: MockNUMANode(2, 8),
				},
			},
			expResult: 3,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.topo.NumNUMANodes(), "")
		})
	}
}

func TestTopology_NumCoresPerNUMA(t *testing.T) {
	for name, tc := range map[string]struct {
		topo      *Topology
		expResult int
	}{
		"nil": {},
		"empty": {
			topo: &Topology{},
		},
		"no cores": {
			topo: &Topology{
				NUMANodes: NodeMap{
					0: MockNUMANode(0, 0),
				},
			},
		},
		"one NUMA": {
			topo: &Topology{
				NUMANodes: NodeMap{
					0: MockNUMANode(0, 6),
				},
			},
			expResult: 6,
		},
		"multiple NUMA": {
			topo: &Topology{
				NUMANodes: NodeMap{
					0: MockNUMANode(0, 8),
					1: MockNUMANode(1, 8),
					2: MockNUMANode(2, 8),
				},
			},
			expResult: 8,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.topo.NumCoresPerNUMA(), "")
		})
	}
}

func TestHardware_DeviceType_String(t *testing.T) {
	for name, tc := range map[string]struct {
		devType   DeviceType
		expResult string
	}{
		"network": {
			devType:   DeviceTypeNetInterface,
			expResult: "network interface",
		},
		"OFI domain": {
			devType:   DeviceTypeOFIDomain,
			expResult: "OFI domain",
		},
		"unknown": {
			devType:   DeviceTypeUnknown,
			expResult: "unknown device type",
		},
		"not recognized": {
			devType:   DeviceType(0xffffffff),
			expResult: "unknown device type",
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.devType.String(), "")
		})
	}
}
