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
							{
								Name:    "test0",
								Type:    DeviceTypeNetInterface,
								PCIAddr: *common.MustNewPCIAddress("0000:01:01.1"),
							},
						},
					),
				},
			},
			expResult: map[string]*PCIDevice{
				"test0": {
					Name:    "test0",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *common.MustNewPCIAddress("0000:01:01.1"),
				},
			},
		},
		"multi device": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: MockNUMANode(0, 8).WithDevices(
						[]*PCIDevice{
							{
								Name:    "test0",
								Type:    DeviceTypeNetInterface,
								PCIAddr: *common.MustNewPCIAddress("0000:01:01.1"),
							},
							{
								Name:    "test1",
								Type:    DeviceTypeOFIDomain,
								PCIAddr: *common.MustNewPCIAddress("0000:01:01.1"),
							},
							{
								Name:    "test2",
								Type:    DeviceTypeNetInterface,
								PCIAddr: *common.MustNewPCIAddress("0000:01:02.1"),
							},
							{
								Name:    "test3",
								Type:    DeviceTypeUnknown,
								PCIAddr: *common.MustNewPCIAddress("0000:01:02.1"),
							},
						},
					),
					1: MockNUMANode(1, 8).WithDevices(
						[]*PCIDevice{
							{
								Name:    "test4",
								Type:    DeviceTypeNetInterface,
								PCIAddr: *common.MustNewPCIAddress("0000:02:01.1"),
							},
							{
								Name:    "test5",
								Type:    DeviceTypeOFIDomain,
								PCIAddr: *common.MustNewPCIAddress("0000:02:01.1"),
							},
							{
								Name:    "test6",
								Type:    DeviceTypeUnknown,
								PCIAddr: *common.MustNewPCIAddress("0000:02:02.1"),
							},
						},
					),
				},
			},
			expResult: map[string]*PCIDevice{
				"test0": {
					Name:    "test0",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *common.MustNewPCIAddress("0000:01:01.1"),
				},
				"test1": {
					Name:    "test1",
					Type:    DeviceTypeOFIDomain,
					PCIAddr: *common.MustNewPCIAddress("0000:01:01.1"),
				},
				"test2": {
					Name:    "test2",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *common.MustNewPCIAddress("0000:01:02.1"),
				},
				"test3": {
					Name:    "test3",
					Type:    DeviceTypeUnknown,
					PCIAddr: *common.MustNewPCIAddress("0000:01:02.1"),
				},
				"test4": {
					Name:    "test4",
					Type:    DeviceTypeNetInterface,
					PCIAddr: *common.MustNewPCIAddress("0000:02:01.1"),
				},
				"test5": {
					Name:    "test5",
					Type:    DeviceTypeOFIDomain,
					PCIAddr: *common.MustNewPCIAddress("0000:02:01.1"),
				},
				"test6": {
					Name:    "test6",
					Type:    DeviceTypeUnknown,
					PCIAddr: *common.MustNewPCIAddress("0000:02:02.1"),
				},
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

func TestHardware_PCIDevices_Keys(t *testing.T) {
	for name, tc := range map[string]struct {
		devices   PCIDevices
		expResult []string
	}{
		"nil": {
			expResult: []string{},
		},
		"empty": {
			devices:   PCIDevices{},
			expResult: []string{},
		},
		"keys": {
			devices: PCIDevices{
				*common.MustNewPCIAddress("0000:01:01.1"): []*PCIDevice{
					{
						Name:    "test0",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *common.MustNewPCIAddress("0000:01:01.1"),
					},
					{
						Name:    "test1",
						Type:    DeviceTypeOFIDomain,
						PCIAddr: *common.MustNewPCIAddress("0000:01:01.1"),
					},
				},
				*common.MustNewPCIAddress("0000:01:02.1"): []*PCIDevice{
					{
						Name:    "test2",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *common.MustNewPCIAddress("0000:01:02.1"),
					},
					{
						Name:    "test3",
						Type:    DeviceTypeUnknown,
						PCIAddr: *common.MustNewPCIAddress("0000:01:02.1"),
					},
				},
				*common.MustNewPCIAddress("0000:01:03.1"): []*PCIDevice{},
			},
			expResult: []string{"0000:01:01.1", "0000:01:02.1", "0000:01:03.1"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.devices.Keys()
			t.Logf("result: %v", result)
			resultStr := make([]string, len(result))
			for i, key := range result {
				resultStr[i] = key.String()
			}

			if diff := cmp.Diff(tc.expResult, resultStr); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_PCIDevices_Add(t *testing.T) {
	for name, tc := range map[string]struct {
		devices   PCIDevices
		newDev    *PCIDevice
		expResult PCIDevices
	}{
		"nil": {},
		"add nil Device": {
			devices:   PCIDevices{},
			expResult: PCIDevices{},
		},
		"add to empty": {
			devices: PCIDevices{},
			newDev: &PCIDevice{
				Name:    "test1",
				Type:    DeviceTypeNetInterface,
				PCIAddr: *common.MustNewPCIAddress("0000:01:01.01"),
			},
			expResult: PCIDevices{
				*common.MustNewPCIAddress("0000:01:01.01"): {
					{
						Name:    "test1",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *common.MustNewPCIAddress("0000:01:01.01"),
					},
				},
			},
		},
		"add to existing": {
			devices: PCIDevices{
				*common.MustNewPCIAddress("0000:01:01.01"): {
					{
						Name:    "test1",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *common.MustNewPCIAddress("0000:01:01.01"),
					},
				},
				*common.MustNewPCIAddress("0000:01:02.01"): {
					{
						Name:    "test2",
						Type:    DeviceTypeUnknown,
						PCIAddr: *common.MustNewPCIAddress("0000:01:02.01"),
					},
				},
			},
			newDev: &PCIDevice{
				Name:    "test3",
				Type:    DeviceTypeOFIDomain,
				PCIAddr: *common.MustNewPCIAddress("0000:01:01.01"),
			},
			expResult: PCIDevices{
				*common.MustNewPCIAddress("0000:01:01.01"): {
					{
						Name:    "test1",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *common.MustNewPCIAddress("0000:01:01.01"),
					},
					{
						Name:    "test3",
						Type:    DeviceTypeOFIDomain,
						PCIAddr: *common.MustNewPCIAddress("0000:01:01.01"),
					},
				},
				*common.MustNewPCIAddress("0000:01:02.01"): {
					{
						Name:    "test2",
						Type:    DeviceTypeUnknown,
						PCIAddr: *common.MustNewPCIAddress("0000:01:02.01"),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.devices.Add(tc.newDev)

			if diff := cmp.Diff(tc.expResult, tc.devices); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
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
