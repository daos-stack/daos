//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestHardware_VirtualDevice(t *testing.T) {
	mockPCIDev := mockPCIDevice("testdev")

	for name, tc := range map[string]struct {
		dev       *VirtualDevice
		expName   string
		expType   DeviceType
		expPCIDev *PCIDevice
	}{
		"nil": {},
		"no PCI dev": {
			dev: &VirtualDevice{
				Name: "testname",
				Type: DeviceTypeNetInterface,
			},
			expName: "testname",
			expType: DeviceTypeNetInterface,
		},
		"PCI dev": {
			dev: &VirtualDevice{
				Name:          "testname",
				Type:          DeviceTypeNetInterface,
				BackingDevice: mockPCIDev,
			},
			expName:   "testname",
			expType:   DeviceTypeNetInterface,
			expPCIDev: mockPCIDev,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expName, tc.dev.DeviceName(), "")
			common.AssertEqual(t, tc.expType, tc.dev.DeviceType(), "")
			common.AssertEqual(t, tc.expPCIDev, tc.dev.PCIDevice(), "")
		})
	}
}

func TestHardware_Topology_AllDevices(t *testing.T) {
	for name, tc := range map[string]struct {
		topo      *Topology
		expResult map[string]Device
	}{
		"nil": {
			expResult: make(map[string]Device),
		},
		"no NUMA nodes": {
			topo:      &Topology{},
			expResult: make(map[string]Device),
		},
		"no PCI addrs": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: MockNUMANode(0, 8),
				},
			},
			expResult: make(map[string]Device),
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
			expResult: map[string]Device{
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
			expResult: map[string]Device{
				"test001": mockPCIDevice("test0", 1, 1, 0).withType(DeviceTypeNetInterface),
				"test101": mockPCIDevice("test1", 1, 1, 0).withType(DeviceTypeOFIDomain),
				"test201": mockPCIDevice("test2", 1, 2, 1).withType(DeviceTypeNetInterface),
				"test301": mockPCIDevice("test3", 1, 2, 1).withType(DeviceTypeUnknown),
				"test402": mockPCIDevice("test4", 2, 1, 1).withType(DeviceTypeNetInterface),
				"test502": mockPCIDevice("test5", 2, 1, 1).withType(DeviceTypeOFIDomain),
				"test602": mockPCIDevice("test6", 2, 2, 1).withType(DeviceTypeNetInterface),
			},
		},
		"virtual devices": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: MockNUMANode(0, 8).WithDevices(
						[]*PCIDevice{
							mockPCIDevice("test").withType(DeviceTypeNetInterface),
						},
					),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
					},
					{
						Name: "virt1",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			expResult: map[string]Device{
				"test00": mockPCIDevice("test").withType(DeviceTypeNetInterface),
				"virt0": &VirtualDevice{
					Name: "virt0",
					Type: DeviceTypeNetInterface,
				},
				"virt1": &VirtualDevice{
					Name: "virt1",
					Type: DeviceTypeNetInterface,
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
			test.AssertEqual(t, tc.expResult, tc.topo.NumNUMANodes(), "")
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
			test.AssertEqual(t, tc.expResult, tc.topo.NumCoresPerNUMA(), "")
		})
	}
}

func TestHardware_Topology_AddDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		topo      *Topology
		numaNode  uint
		device    *PCIDevice
		expResult *Topology
		expErr    error
	}{
		"nil topology": {
			device: &PCIDevice{
				Name:    "test",
				PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
			},
			expErr: errors.New("nil"),
		},
		"nil input": {
			topo:      &Topology{},
			expErr:    errors.New("nil"),
			expResult: &Topology{},
		},
		"add to empty": {
			topo:     &Topology{},
			numaNode: 1,
			device: &PCIDevice{
				Name:    "test",
				PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					1: MockNUMANode(1, 0).WithDevices([]*PCIDevice{
						{
							Name:    "test",
							PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
						},
					}),
				},
			},
		},
		"add to existing node": {
			topo: &Topology{
				NUMANodes: NodeMap{
					1: MockNUMANode(1, 6).WithDevices([]*PCIDevice{
						{
							Name:    "test0",
							PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
						},
					}),
				},
			},
			numaNode: 1,
			device: &PCIDevice{
				Name:    "test1",
				PCIAddr: *MustNewPCIAddress("0000:00:00.2"),
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					1: MockNUMANode(1, 6).WithDevices([]*PCIDevice{
						{
							Name:    "test0",
							PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
						},
						{
							Name:    "test1",
							PCIAddr: *MustNewPCIAddress("0000:00:00.2"),
						},
					}),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := tc.topo.AddDevice(tc.numaNode, tc.device)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.topo); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_Topology_AddVirtualDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		topo      *Topology
		device    *VirtualDevice
		expResult *Topology
		expErr    error
	}{
		"nil topology": {
			device: &VirtualDevice{
				Name: "test",
			},
			expErr: errors.New("nil"),
		},
		"nil input": {
			topo:      &Topology{},
			expErr:    errors.New("nil"),
			expResult: &Topology{},
		},
		"add to empty": {
			topo: &Topology{},
			device: &VirtualDevice{
				Name: "test",
			},
			expResult: &Topology{
				VirtualDevices: []*VirtualDevice{
					{
						Name: "test",
					},
				},
			},
		},
		"add to existing": {
			topo: &Topology{
				VirtualDevices: []*VirtualDevice{
					{
						Name: "test0",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			device: &VirtualDevice{
				Name: "test1",
				Type: DeviceTypeNetInterface,
			},
			expResult: &Topology{
				VirtualDevices: []*VirtualDevice{
					{
						Name: "test0",
						Type: DeviceTypeNetInterface,
					},
					{
						Name: "test1",
						Type: DeviceTypeNetInterface,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := tc.topo.AddVirtualDevice(tc.device)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.topo); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_Topology_Merge(t *testing.T) {
	testNuma := func(idx int) *NUMANode {
		nodes := []*NUMANode{
			MockNUMANode(1, 4).
				WithDevices([]*PCIDevice{
					{
						Name:      "test0",
						PCIAddr:   *MustNewPCIAddress("0000:00:00.1"),
						LinkSpeed: 60,
					},
				}).
				WithCPUCores([]CPUCore{}).
				WithPCIBuses([]*PCIBus{
					{
						LowAddress:  *MustNewPCIAddress("0000:00:00.0"),
						HighAddress: *MustNewPCIAddress("0000:05:00.0"),
					},
				}),
			MockNUMANode(2, 4).
				WithDevices([]*PCIDevice{
					{
						Name:    "test1",
						PCIAddr: *MustNewPCIAddress("0000:0a:00.1"),
					},
				}).
				WithCPUCores([]CPUCore{}).
				WithPCIBuses([]*PCIBus{
					{
						LowAddress:  *MustNewPCIAddress("0000:05:00.0"),
						HighAddress: *MustNewPCIAddress("0000:0f:00.0"),
					},
				}),
		}
		return nodes[idx]
	}

	for name, tc := range map[string]struct {
		topo      *Topology
		input     *Topology
		expResult *Topology
		expErr    error
	}{
		"nil base": {
			input:  &Topology{},
			expErr: errors.New("nil"),
		},
		"nil input": {
			topo:   &Topology{},
			expErr: errors.New("nil"),
		},
		"all empties": {
			topo:      &Topology{},
			input:     &Topology{},
			expResult: &Topology{},
		},
		"add to empty": {
			topo: &Topology{},
			input: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
			},
		},
		"add to existing NUMA node": {
			topo: &Topology{
				NUMANodes: NodeMap{
					1: MockNUMANode(1, 4).
						WithCPUCores([]CPUCore{}).
						WithPCIBuses([]*PCIBus{
							{
								LowAddress:  *MustNewPCIAddress("0000:00:00.0"),
								HighAddress: *MustNewPCIAddress("0000:05:00.0"),
							},
						}),
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{
					1: MockNUMANode(1, 0).
						WithDevices([]*PCIDevice{
							{
								Name:      "test0",
								PCIAddr:   *MustNewPCIAddress("0000:00:00.1"),
								LinkSpeed: 60,
							},
						}),
				},
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					1: MockNUMANode(1, 4).
						WithDevices([]*PCIDevice{
							{
								Name:      "test0",
								PCIAddr:   *MustNewPCIAddress("0000:00:00.1"),
								LinkSpeed: 60,
							},
						}).
						WithCPUCores([]CPUCore{}).
						WithPCIBuses([]*PCIBus{
							{
								LowAddress:  *MustNewPCIAddress("0000:00:00.0"),
								HighAddress: *MustNewPCIAddress("0000:05:00.0"),
							},
						}),
				},
			},
		},
		"no intersection": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{
					testNuma(1).ID: testNuma(1),
				},
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
					testNuma(1).ID: testNuma(1),
				},
			},
		},
		"add to same NUMA node": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: MockNUMANode(testNuma(0).ID, 0).
						WithDevices([]*PCIDevice{
							{
								Name:    "test1",
								Type:    DeviceTypeNetInterface,
								PCIAddr: *MustNewPCIAddress("0000:00:00.2"),
							},
						}).
						WithCPUCores([]CPUCore{
							{
								ID: 4,
							},
						}).
						WithPCIBuses([]*PCIBus{
							{
								LowAddress:  *MustNewPCIAddress("0000:0f:00.0"),
								HighAddress: *MustNewPCIAddress("0000:20:00.0"),
							},
						}),
				},
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: MockNUMANode(testNuma(0).ID, 5).
						WithDevices([]*PCIDevice{
							testNuma(0).PCIDevices[*MustNewPCIAddress("0000:00:00.1")][0],
							{
								Name:    "test1",
								Type:    DeviceTypeNetInterface,
								PCIAddr: *MustNewPCIAddress("0000:00:00.2"),
							},
						}).
						WithPCIBuses([]*PCIBus{
							testNuma(0).PCIBuses[0],
							{
								LowAddress:  *MustNewPCIAddress("0000:0f:00.0"),
								HighAddress: *MustNewPCIAddress("0000:20:00.0"),
							},
						}),
				},
			},
		},
		"update": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: MockNUMANode(testNuma(0).ID, 5).
						WithDevices([]*PCIDevice{
							{
								Name:    "test0",
								Type:    DeviceTypeNetInterface,
								PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
							},
							{
								Name:      "test1",
								Type:      DeviceTypeNetInterface,
								PCIAddr:   *MustNewPCIAddress("0000:00:00.2"),
								LinkSpeed: 75,
							},
						}).
						WithPCIBuses([]*PCIBus{
							testNuma(0).PCIBuses[0],
							{
								LowAddress:  *MustNewPCIAddress("0000:0f:00.0"),
								HighAddress: *MustNewPCIAddress("0000:20:00.0"),
							},
						}),
				},
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: MockNUMANode(testNuma(0).ID, 5).
						WithDevices([]*PCIDevice{
							{
								Name:      "test0",
								Type:      DeviceTypeNetInterface,
								PCIAddr:   *MustNewPCIAddress("0000:00:00.1"),
								LinkSpeed: 60,
							},
							{
								Name:      "test1",
								Type:      DeviceTypeNetInterface,
								PCIAddr:   *MustNewPCIAddress("0000:00:00.2"),
								LinkSpeed: 75,
							},
						}).
						WithPCIBuses([]*PCIBus{
							testNuma(0).PCIBuses[0],
							{
								LowAddress:  *MustNewPCIAddress("0000:0f:00.0"),
								HighAddress: *MustNewPCIAddress("0000:20:00.0"),
							},
						}),
				},
			},
		},
		"add virtual devices": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			input: &Topology{
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt1",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
					},
					{
						Name: "virt1",
						Type: DeviceTypeNetInterface,
					},
				},
			},
		},
		"update virtual devices": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
					},
					{
						Name: "virt1",
					},
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: MockNUMANode(testNuma(0).ID, 5).
						WithDevices([]*PCIDevice{
							{
								Name:    "test0",
								Type:    DeviceTypeNetInterface,
								PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
							},
						}),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						BackingDevice: &PCIDevice{
							Name:    "test0",
							Type:    DeviceTypeNetInterface,
							PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
						},
					},
					{
						Name: "virt1",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			expResult: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: MockNUMANode(testNuma(0).ID, 5).
						WithDevices([]*PCIDevice{
							{
								Name:      "test0",
								Type:      DeviceTypeNetInterface,
								PCIAddr:   *MustNewPCIAddress("0000:00:00.1"),
								LinkSpeed: 60,
							},
						}).
						WithPCIBuses([]*PCIBus{
							testNuma(0).PCIBuses[0],
						}),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
						BackingDevice: &PCIDevice{
							Name:      "test0",
							Type:      DeviceTypeNetInterface,
							PCIAddr:   *MustNewPCIAddress("0000:00:00.1"),
							LinkSpeed: 60,
						},
					},
					{
						Name: "virt1",
						Type: DeviceTypeNetInterface,
					},
				},
			},
		},
		"incoming virtual dev named after HW dev": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "test0",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			expErr: errors.New("same name"),
		},
		"updated backing device not mapped": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
						BackingDevice: &PCIDevice{
							Name:    "doesnotexist",
							Type:    DeviceTypeNetInterface,
							PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
						},
					},
				},
			},
			expErr: errors.New("does not exist"),
		},
		"new backing device not mapped": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
						BackingDevice: &PCIDevice{
							Name:    "doesnotexist",
							Type:    DeviceTypeNetInterface,
							PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
						},
					},
				},
			},
			expErr: errors.New("does not exist"),
		},
		"updated backing device is virtual": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
					},
					{
						Name: "virt1",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt1",
						Type: DeviceTypeNetInterface,
						BackingDevice: &PCIDevice{
							Name:    "virt0",
							Type:    DeviceTypeNetInterface,
							PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
						},
					},
				},
			},
			expErr: errors.New("not a PCI device"),
		},
		"new backing device is virtual": {
			topo: &Topology{
				NUMANodes: NodeMap{
					testNuma(0).ID: testNuma(0),
				},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt0",
						Type: DeviceTypeNetInterface,
					},
				},
			},
			input: &Topology{
				NUMANodes: NodeMap{},
				VirtualDevices: []*VirtualDevice{
					{
						Name: "virt1",
						Type: DeviceTypeNetInterface,
						BackingDevice: &PCIDevice{
							Name:    "virt0",
							Type:    DeviceTypeNetInterface,
							PCIAddr: *MustNewPCIAddress("0000:00:00.1"),
						},
					},
				},
			},
			expErr: errors.New("not a PCI device"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := tc.topo.Merge(tc.input)

			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expResult, tc.topo, test.CmpOptIgnoreFieldAnyType("NUMANode")); diff != "" {
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
			test.AssertEqual(t, tc.expResult, tc.devType.String(), "")
		})
	}
}

func TestHardware_NewTopologyFactory(t *testing.T) {
	for name, tc := range map[string]struct {
		input     []*WeightedTopologyProvider
		expResult *TopologyFactory
	}{
		"empty": {
			expResult: &TopologyFactory{
				providers: []TopologyProvider{},
			},
		},
		"one provider": {
			input: []*WeightedTopologyProvider{
				{
					Provider: &MockTopologyProvider{},
					Weight:   1,
				},
			},
			expResult: &TopologyFactory{
				providers: []TopologyProvider{
					&MockTopologyProvider{},
				},
			},
		},
		"multiple providers": {
			input: []*WeightedTopologyProvider{
				{
					Provider: &MockTopologyProvider{
						GetTopoErr: errors.New("provider 1"),
					},
					Weight: 1,
				},
				{
					Provider: &MockTopologyProvider{
						GetTopoErr: errors.New("provider 3"),
					},
					Weight: 3,
				},
				{
					Provider: &MockTopologyProvider{
						GetTopoErr: errors.New("provider 2"),
					},
					Weight: 2,
				},
				{
					Provider: &MockTopologyProvider{
						GetTopoErr: errors.New("provider 4"),
					},
					Weight: 4,
				},
			},
			expResult: &TopologyFactory{
				providers: []TopologyProvider{
					&MockTopologyProvider{
						GetTopoErr: errors.New("provider 4"),
					},
					&MockTopologyProvider{
						GetTopoErr: errors.New("provider 3"),
					},
					&MockTopologyProvider{
						GetTopoErr: errors.New("provider 2"),
					},
					&MockTopologyProvider{
						GetTopoErr: errors.New("provider 1"),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := NewTopologyFactory(tc.input...)

			if diff := cmp.Diff(tc.expResult, result,
				cmp.AllowUnexported(TopologyFactory{}),
				test.CmpOptEquateErrorMessages(),
			); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_TopologyFactory_GetTopology(t *testing.T) {
	testTopo1 := func() *Topology {
		return &Topology{
			NUMANodes: NodeMap{
				0: MockNUMANode(0, 6).
					WithDevices([]*PCIDevice{
						{
							Name: "net0",
							Type: DeviceTypeNetInterface,
						},
						{
							Name: "ofi0",
							Type: DeviceTypeOFIDomain,
						},
					}),
				1: MockNUMANode(1, 6).
					WithDevices([]*PCIDevice{
						{
							Name: "net1",
							Type: DeviceTypeNetInterface,
						},
						{
							Name: "ofi1",
							Type: DeviceTypeOFIDomain,
						},
					}),
			},
		}
	}

	testTopo2 := func() *Topology {
		return &Topology{
			NUMANodes: NodeMap{
				0: MockNUMANode(0, 6).
					WithDevices([]*PCIDevice{
						{
							Name: "net0",
							Type: DeviceTypeNetInterface,
						},
						{
							Name: "net2",
							Type: DeviceTypeNetInterface,
						},
					}),
				1: MockNUMANode(1, 6).
					WithDevices([]*PCIDevice{
						{
							Name: "net1",
							Type: DeviceTypeNetInterface,
						},
					}),
			},
		}
	}

	testMerged := testTopo1()
	testMerged.Merge(testTopo2())

	for name, tc := range map[string]struct {
		tf        *TopologyFactory
		expResult *Topology
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"no providers in factory": {
			tf:     &TopologyFactory{},
			expErr: errors.New("no TopologyProviders"),
		},
		"successful provider": {
			tf: &TopologyFactory{
				providers: []TopologyProvider{
					&MockTopologyProvider{
						GetTopoReturn: testTopo1(),
					},
				},
			},
			expResult: testTopo1(),
		},
		"multi provider": {
			tf: &TopologyFactory{
				providers: []TopologyProvider{
					&MockTopologyProvider{
						GetTopoReturn: testTopo1(),
					},
					&MockTopologyProvider{
						GetTopoReturn: testTopo2(),
					},
				},
			},
			expResult: testMerged,
		},
		"one provider fails": {
			tf: &TopologyFactory{
				providers: []TopologyProvider{
					&MockTopologyProvider{
						GetTopoErr: errors.New("mock GetTopology"),
					},
					&MockTopologyProvider{
						GetTopoReturn: testTopo2(),
					},
				},
			},
			expErr: errors.New("mock GetTopology"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.tf.GetTopology(context.Background())

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}
