//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/google/go-cmp/cmp"
)

func TestHardware_Topology_AllDevices(t *testing.T) {
	for name, tc := range map[string]struct {
		topo      *Topology
		expResult map[string]*Device
	}{
		"nil": {
			expResult: make(map[string]*Device),
		},
		"no NUMA nodes": {
			topo:      &Topology{},
			expResult: make(map[string]*Device),
		},
		"no PCI addrs": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: {
						ID:       0,
						NumCores: 8,
					},
				},
			},
			expResult: make(map[string]*Device),
		},
		"no devices": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: {
						ID:       0,
						NumCores: 8,
						Devices: PCIDevices{
							"0000:01:01.1": []*Device{},
						},
					},
				},
			},
			expResult: make(map[string]*Device),
		},
		"single device": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: {
						ID:       0,
						NumCores: 8,
						Devices: PCIDevices{
							"0000:01:01.1": []*Device{
								{
									Name:    "test0",
									Type:    DeviceTypeNetInterface,
									PCIAddr: "0000:01:01.1",
								},
							},
						},
					},
				},
			},
			expResult: map[string]*Device{
				"test0": {
					Name:    "test0",
					Type:    DeviceTypeNetInterface,
					PCIAddr: "0000:01:01.1",
				},
			},
		},
		"multi device": {
			topo: &Topology{
				NUMANodes: map[uint]*NUMANode{
					0: {
						ID:       0,
						NumCores: 8,
						Devices: PCIDevices{
							"0000:01:01.1": []*Device{
								{
									Name:    "test0",
									Type:    DeviceTypeNetInterface,
									PCIAddr: "0000:01:01.1",
								},
								{
									Name:    "test1",
									Type:    DeviceTypeOFIDomain,
									PCIAddr: "0000:01:01.1",
								},
							},
							"0000:01:02.1": []*Device{
								{
									Name:    "test2",
									Type:    DeviceTypeNetInterface,
									PCIAddr: "0000:01:02.1",
								},
								{
									Name:    "test3",
									Type:    DeviceTypeUnknown,
									PCIAddr: "0000:01:02.1",
								},
							},
						},
					},
					1: {
						ID:       1,
						NumCores: 8,
						Devices: PCIDevices{
							"0000:02:01.1": []*Device{
								{
									Name:    "test4",
									Type:    DeviceTypeNetInterface,
									PCIAddr: "0000:02:01.1",
								},
								{
									Name:    "test5",
									Type:    DeviceTypeOFIDomain,
									PCIAddr: "0000:02:01.1",
								},
							},
							"0000:02:02.1": []*Device{
								{
									Name:    "test6",
									Type:    DeviceTypeUnknown,
									PCIAddr: "0000:02:02.1",
								},
							},
						},
					},
				},
			},
			expResult: map[string]*Device{
				"test0": {
					Name:    "test0",
					Type:    DeviceTypeNetInterface,
					PCIAddr: "0000:01:01.1",
				},
				"test1": {
					Name:    "test1",
					Type:    DeviceTypeOFIDomain,
					PCIAddr: "0000:01:01.1",
				},
				"test2": {
					Name:    "test2",
					Type:    DeviceTypeNetInterface,
					PCIAddr: "0000:01:02.1",
				},
				"test3": {
					Name:    "test3",
					Type:    DeviceTypeUnknown,
					PCIAddr: "0000:01:02.1",
				},
				"test4": {
					Name:    "test4",
					Type:    DeviceTypeNetInterface,
					PCIAddr: "0000:02:01.1",
				},
				"test5": {
					Name:    "test5",
					Type:    DeviceTypeOFIDomain,
					PCIAddr: "0000:02:01.1",
				},
				"test6": {
					Name:    "test6",
					Type:    DeviceTypeUnknown,
					PCIAddr: "0000:02:02.1",
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.topo.AllDevices()

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
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
				"0000:01:01.1": []*Device{
					{
						Name:    "test0",
						Type:    DeviceTypeNetInterface,
						PCIAddr: "0000:01:01.1",
					},
					{
						Name:    "test1",
						Type:    DeviceTypeOFIDomain,
						PCIAddr: "0000:01:01.1",
					},
				},
				"0000:01:02.1": []*Device{
					{
						Name:    "test2",
						Type:    DeviceTypeNetInterface,
						PCIAddr: "0000:01:02.1",
					},
					{
						Name:    "test3",
						Type:    DeviceTypeUnknown,
						PCIAddr: "0000:01:02.1",
					},
				},
				"0000:01:03.1": []*Device{},
			},
			expResult: []string{"0000:01:01.1", "0000:01:02.1", "0000:01:03.1"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.devices.Keys()

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_PCIDevices_Add(t *testing.T) {
	for name, tc := range map[string]struct {
		devices   PCIDevices
		newDev    *Device
		expResult PCIDevices
	}{
		"nil": {},
		"add nil Device": {
			devices:   PCIDevices{},
			expResult: PCIDevices{},
		},
		"add to empty": {
			devices: PCIDevices{},
			newDev: &Device{
				Name:         "test1",
				Type:         DeviceTypeNetInterface,
				NUMAAffinity: 1,
				PCIAddr:      "0000:01:01.01",
			},
			expResult: PCIDevices{
				"0000:01:01.01": {
					{
						Name:         "test1",
						Type:         DeviceTypeNetInterface,
						NUMAAffinity: 1,
						PCIAddr:      "0000:01:01.01",
					},
				},
			},
		},
		"add to existing": {
			devices: PCIDevices{
				"0000:01:01.01": {
					{
						Name:         "test1",
						Type:         DeviceTypeNetInterface,
						NUMAAffinity: 1,
						PCIAddr:      "0000:01:01.01",
					},
				},
				"0000:01:02.01": {
					{
						Name:         "test2",
						Type:         DeviceTypeUnknown,
						NUMAAffinity: 1,
						PCIAddr:      "0000:01:02.01",
					},
				},
			},
			newDev: &Device{
				Name:         "test3",
				Type:         DeviceTypeOFIDomain,
				NUMAAffinity: 1,
				PCIAddr:      "0000:01:01.01",
			},
			expResult: PCIDevices{
				"0000:01:01.01": {
					{
						Name:         "test1",
						Type:         DeviceTypeNetInterface,
						NUMAAffinity: 1,
						PCIAddr:      "0000:01:01.01",
					},
					{
						Name:         "test3",
						Type:         DeviceTypeOFIDomain,
						NUMAAffinity: 1,
						PCIAddr:      "0000:01:01.01",
					},
				},
				"0000:01:02.01": {
					{
						Name:         "test2",
						Type:         DeviceTypeUnknown,
						NUMAAffinity: 1,
						PCIAddr:      "0000:01:02.01",
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
