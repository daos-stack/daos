//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func mockPCIBus(args ...uint8) *PCIBus {
	switch len(args) {
	case 0:
		args = []uint8{0, 0, 1}
	case 1:
		args = append(args, []uint8{0, 1}...)
	case 2:
		args = append(args, args[1]+1)
	}

	return &PCIBus{
		LowAddress:  *MockPCIAddress(args[0], args[1]),
		HighAddress: *MockPCIAddress(args[0], args[2]),
	}
}

func mockPCIDevice(name string, addr ...uint8) *PCIDevice {
	if len(addr) == 0 {
		addr = []uint8{0}
	}

	return &PCIDevice{
		Name:    fmt.Sprintf("%s%02d", name, addr[0]),
		PCIAddr: *MockPCIAddress(addr...),
	}
}

func (d *PCIDevice) withType(t DeviceType) *PCIDevice {
	d.Type = t
	return d
}

func (d *PCIDevice) withLinkSpeed(ls float64) *PCIDevice {
	d.LinkSpeed = ls
	return d
}

func TestHardware_NewPCIAddress(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStr string
		expVMD  *PCIAddress
		expErr  error
	}{
		"valid": {
			addrStr: "0000:80:00.0",
		},
		"invalid": {
			addrStr: "0000:gg:00.0",
			expErr:  errors.New("parsing \"gg\""),
		},
		"vmd address": {
			addrStr: "0000:5d:05.5",
		},
		"vmd backing device address": {
			addrStr: "ffffff:01:00.0",
			expVMD: &PCIAddress{
				Bus:      0xff,
				Device:   0xff,
				Function: 0xff,
			},
		},
		"short vmd backing device address": {
			addrStr: "50505:05:00.0",
			expVMD: &PCIAddress{
				Bus:      0x05,
				Device:   0x05,
				Function: 0x05,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			addr, err := NewPCIAddress(tc.addrStr)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expVMD, addr.VMDAddr); diff != "" {
				t.Fatalf("unexpected VMD address (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestHardware_PCIAddress_Equals(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStr1 string
		addrStr2 string
		expEqual bool
	}{
		"same": {
			addrStr1: "0000:80:00.0",
			addrStr2: "0000:80:00.0",
			expEqual: true,
		},
		"different domain": {
			addrStr1: "0000:80:00.0",
			addrStr2: "0001:80:00.0",
			expEqual: false,
		},
		"different bus": {
			addrStr1: "0000:80:00.0",
			addrStr2: "0000:81:00.0",
			expEqual: false,
		},
		"different device": {
			addrStr1: "0000:80:00.0",
			addrStr2: "0000:80:01.0",
			expEqual: false,
		},
		"different function": {
			addrStr1: "0000:80:00.0",
			addrStr2: "0000:80:00.1",
			expEqual: false,
		},
		"vmd backing device same": {
			addrStr1: "ffffff:01:00.0",
			addrStr2: "ffffff:01:00.0",
			expEqual: true,
		},
		"vmd backing device different": {
			addrStr1: "ffffff:01:00.0",
			addrStr2: "fffffe:01:00.0",
			expEqual: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			addr1, err := NewPCIAddress(tc.addrStr1)
			if err != nil {
				t.Fatal(err)
			}
			addr2, err := NewPCIAddress(tc.addrStr2)
			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(tc.expEqual, addr1.Equals(addr2)); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestHardware_NewPCIAddressSet(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStrs    []string
		expAddrStr  string
		expAddrStrs []string
		expErr      error
	}{
		"valid": {
			addrStrs: []string{
				"0000:7f:00.1", "0000:81:00.0", "0000:7f:01.0", "0000:80:00.0",
				"0000:7f:00.0", "0000:7e:00.0", "5d0505:01:00.0",
			},
			expAddrStr: "0000:7e:00.0 0000:7f:00.0 0000:7f:00.1 0000:7f:01.0 " +
				"0000:80:00.0 0000:81:00.0 5d0505:01:00.0",
			expAddrStrs: []string{
				"0000:7e:00.0", "0000:7f:00.0", "0000:7f:00.1", "0000:7f:01.0",
				"0000:80:00.0", "0000:81:00.0", "5d0505:01:00.0",
			},
		},
		"multiple vmd backing device addresses": {
			addrStrs: []string{
				"d70505:03:00.0",
				"d70505:01:00.0",
				"5d0505:03:00.0",
				"5d0505:01:00.0",
			},
			expAddrStr: "5d0505:01:00.0 5d0505:03:00.0 d70505:01:00.0 d70505:03:00.0",
			expAddrStrs: []string{
				"5d0505:01:00.0",
				"5d0505:03:00.0",
				"d70505:01:00.0",
				"d70505:03:00.0",
			},
		},
		"vmd backing device": {
			addrStrs: []string{
				"050505:03:00.0", "050505:01:00.0",
			},
			expAddrStr: "050505:01:00.0 050505:03:00.0",
			expAddrStrs: []string{
				"050505:01:00.0", "050505:03:00.0",
			},
		},
		"invalid": {
			addrStrs: []string{"0000:7f.00.0"},
			expErr:   errors.New("bdf format"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrSet, err := NewPCIAddressSet(tc.addrStrs...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expAddrStr, addrSet.String()); diff != "" {
				t.Fatalf("unexpected string (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expAddrStrs, addrSet.Strings()); diff != "" {
				t.Fatalf("unexpected list (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestHardware_PCIAddressSet_Addresses(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStrs []string
		expAddrs []*PCIAddress
		expErr   error
	}{
		"multiple vmd backing device addresses": {
			addrStrs: []string{
				"d70505:03:00.0",
				"d70505:01:00.0",
				"5d0505:03:00.0",
				"5d0505:01:00.0",
			},
			expAddrs: []*PCIAddress{
				MustNewPCIAddress("5d0505:01:00.0"),
				MustNewPCIAddress("5d0505:03:00.0"),
				MustNewPCIAddress("d70505:01:00.0"),
				MustNewPCIAddress("d70505:03:00.0"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrSet, err := NewPCIAddressSet(tc.addrStrs...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expAddrs, addrSet.Addresses()); diff != "" {
				t.Fatalf("unexpected list (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestHardware_PCIAddressSet_Intersect(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStrs          []string
		intersectAddrStrs []string
		expAddrStrs       []string
	}{
		"partial": {
			addrStrs: []string{
				"0000:7f:00.1", "0000:81:00.0", "0000:7f:01.0", "0000:80:00.0",
				"0000:7f:00.0", "0000:7e:00.0",
			},
			intersectAddrStrs: []string{
				"0000:7e:00.0", "0000:7f:00.0", "0000:7f:00.1", "0000:7f:01.1",
			},
			expAddrStrs: []string{
				"0000:7e:00.0", "0000:7f:00.0", "0000:7f:00.1",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrSet, err := NewPCIAddressSet(tc.addrStrs...)
			if err != nil {
				t.Fatal(err)
			}

			intersectAddrSet, err := NewPCIAddressSet(tc.intersectAddrStrs...)
			if err != nil {
				t.Fatal(err)
			}

			intersection := addrSet.Intersect(intersectAddrSet)

			if diff := cmp.Diff(tc.expAddrStrs, intersection.Strings()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestHardware_PCIAddressSet_Difference(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStrs           []string
		differenceAddrStrs []string
		expAddrStrs        []string
	}{
		"partial": {
			addrStrs: []string{
				"0000:7f:00.1", "0000:81:00.0", "0000:7f:01.0", "0000:80:00.0",
				"0000:7f:00.0", "0000:7e:00.0",
			},
			differenceAddrStrs: []string{
				"0000:7e:00.0", "0000:7f:00.0", "0000:7f:00.1", "0000:7f:01.1",
			},
			expAddrStrs: []string{
				"0000:7f:01.0", "0000:80:00.0", "0000:81:00.0",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrSet, err := NewPCIAddressSet(tc.addrStrs...)
			if err != nil {
				t.Fatal(err)
			}

			differenceAddrSet, err := NewPCIAddressSet(tc.differenceAddrStrs...)
			if err != nil {
				t.Fatal(err)
			}

			difference := addrSet.Difference(differenceAddrSet)

			if diff := cmp.Diff(tc.expAddrStrs, difference.Strings()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestHardware_PCIAddressSet_BackingToVMDAddresses(t *testing.T) {
	for name, tc := range map[string]struct {
		inAddrs     []string
		expOutAddrs []string
		expErr      error
	}{
		"empty": {
			expOutAddrs: []string{},
		},
		"no vmd addresses": {
			inAddrs:     []string{"0000:80:00.0"},
			expOutAddrs: []string{"0000:80:00.0"},
		},
		"single vmd address": {
			inAddrs:     []string{"5d0505:01:00.0"},
			expOutAddrs: []string{"0000:5d:05.5"},
		},
		"multiple vmd backing device addresses": {
			inAddrs: []string{
				"d70505:01:00.0",
				"d70505:03:00.0",
				"5d0505:01:00.0",
				"5d0505:03:00.0",
			},
			expOutAddrs: []string{
				"0000:5d:05.5",
				"0000:d7:05.5",
			},
		},
		"short vmd domain in address": {
			inAddrs:     []string{"5d055:01:00.0"},
			expOutAddrs: []string{"0000:05:d0.55"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrSet, gotErr := NewPCIAddressSet(tc.inAddrs...)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotAddrs, gotErr := addrSet.BackingToVMDAddresses()
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutAddrs, gotAddrs.Strings()); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestHardware_PCIDevices_Get(t *testing.T) {
	testDevs := PCIDevices{
		*MustNewPCIAddress("0000:01:01.1"): []*PCIDevice{
			mockPCIDevice("test0", 0, 1, 1, 1).withType(DeviceTypeNetInterface),
			mockPCIDevice("test1", 0, 1, 1, 1).withType(DeviceTypeOFIDomain),
		},
		*MustNewPCIAddress("0000:01:02.1"): []*PCIDevice{
			mockPCIDevice("test2", 0, 1, 2, 1).withType(DeviceTypeNetInterface),
			mockPCIDevice("test3", 0, 1, 2, 1),
		},
		*MustNewPCIAddress("0000:01:03.1"): []*PCIDevice{},
	}

	for name, tc := range map[string]struct {
		devices PCIDevices
		getKey  *PCIAddress
		expDevs []*PCIDevice
	}{
		"nil devs, nil key": {
			expDevs: nil,
		},
		"nil key": {
			devices: testDevs,
			getKey:  nil,
			expDevs: nil,
		},
		"empty devs": {
			devices: PCIDevices{},
			getKey:  MustNewPCIAddress("0000:80:00.0"),
			expDevs: nil,
		},
		"bad key": {
			devices: testDevs,
			getKey:  MustNewPCIAddress("0000:80:00.0"),
			expDevs: nil,
		},
		"good key": {
			devices: testDevs,
			getKey:  MustNewPCIAddress("0000:01:02.1"),
			expDevs: []*PCIDevice{
				mockPCIDevice("test2", 0, 1, 2, 1).withType(DeviceTypeNetInterface),
				mockPCIDevice("test3", 0, 1, 2, 1),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.devices.Keys()
			resultStr := make([]string, len(result))
			for i, key := range result {
				resultStr[i] = key.String()
			}
			gotDevs := tc.devices.Get(tc.getKey)

			if diff := cmp.Diff(tc.expDevs, gotDevs); diff != "" {
				t.Fatalf("unexpected devices (-want, +got)\n%s\n", diff)
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
				*MustNewPCIAddress("0000:01:01.1"): []*PCIDevice{
					{
						Name:    "test0",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *MustNewPCIAddress("0000:01:01.1"),
					},
					{
						Name:    "test1",
						Type:    DeviceTypeOFIDomain,
						PCIAddr: *MustNewPCIAddress("0000:01:01.1"),
					},
				},
				*MustNewPCIAddress("0000:01:02.1"): []*PCIDevice{
					{
						Name:    "test2",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *MustNewPCIAddress("0000:01:02.1"),
					},
					{
						Name:    "test3",
						Type:    DeviceTypeUnknown,
						PCIAddr: *MustNewPCIAddress("0000:01:02.1"),
					},
				},
				*MustNewPCIAddress("0000:01:03.1"): []*PCIDevice{},
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
		expErr    error
		expResult PCIDevices
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"add nil Device": {
			devices:   PCIDevices{},
			expErr:    errors.New("nil"),
			expResult: PCIDevices{},
		},
		"add to empty": {
			devices: PCIDevices{},
			newDev: &PCIDevice{
				Name:    "test1",
				Type:    DeviceTypeNetInterface,
				PCIAddr: *MustNewPCIAddress("0000:01:01.01"),
			},
			expResult: PCIDevices{
				*MustNewPCIAddress("0000:01:01.01"): {
					{
						Name:    "test1",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *MustNewPCIAddress("0000:01:01.01"),
					},
				},
			},
		},
		"add to existing": {
			devices: PCIDevices{
				*MustNewPCIAddress("0000:01:01.01"): {
					{
						Name:    "test1",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *MustNewPCIAddress("0000:01:01.01"),
					},
				},
				*MustNewPCIAddress("0000:01:02.01"): {
					{
						Name:    "test2",
						Type:    DeviceTypeUnknown,
						PCIAddr: *MustNewPCIAddress("0000:01:02.01"),
					},
				},
			},
			newDev: &PCIDevice{
				Name:    "test3",
				Type:    DeviceTypeOFIDomain,
				PCIAddr: *MustNewPCIAddress("0000:01:01.01"),
			},
			expResult: PCIDevices{
				*MustNewPCIAddress("0000:01:01.01"): {
					{
						Name:    "test1",
						Type:    DeviceTypeNetInterface,
						PCIAddr: *MustNewPCIAddress("0000:01:01.01"),
					},
					{
						Name:    "test3",
						Type:    DeviceTypeOFIDomain,
						PCIAddr: *MustNewPCIAddress("0000:01:01.01"),
					},
				},
				*MustNewPCIAddress("0000:01:02.01"): {
					{
						Name:    "test2",
						Type:    DeviceTypeUnknown,
						PCIAddr: *MustNewPCIAddress("0000:01:02.01"),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := tc.devices.Add(tc.newDev)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, tc.devices); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestHardware_PCIBus(t *testing.T) {
	isNilErr := errors.New("is nil")

	for name, tc := range map[string]struct {
		bus    *PCIBus
		dev    *PCIDevice
		op     string
		expErr error
	}{
		"add device to nil bus": {
			dev:    mockPCIDevice("test"),
			op:     "add",
			expErr: isNilErr,
		},
		"add nil device": {
			bus:    mockPCIBus(),
			op:     "add",
			expErr: isNilErr,
		},
		"nil bus contains device": {
			dev: mockPCIDevice("test"),
			op:  "nil contains",
		},
		"add device to wrong bus": {
			bus:    mockPCIBus(0, 1, 2),
			dev:    mockPCIDevice("test", 0, 3),
			op:     "add",
			expErr: errors.New("not on bus"),
		},
		"add device to right bus": {
			bus: mockPCIBus(0, 1, 4),
			dev: mockPCIDevice("test", 0, 3),
			op:  "add",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var gotErr error

			switch tc.op {
			case "add":
				gotErr = tc.bus.AddDevice(tc.dev)
			case "nil contains":
				contains := tc.bus.Contains(&tc.dev.PCIAddr)
				test.AssertFalse(t, contains, "nil bus shouldn't contain anything")
				return
			default:
				panic("unknown operation")
			}

			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			for _, devAddr := range tc.bus.PCIDevices.Keys() {
				if devAddr.Equals(&tc.dev.PCIAddr) {
					return
				}
			}

			t.Fatalf("device not added to bus")
		})
	}
}

func TestHardware_PCIDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		dev     *PCIDevice
		expName string
		expType DeviceType
	}{
		"nil": {},
		"valid": {
			dev: &PCIDevice{
				Name: "testname",
				Type: DeviceTypeNetInterface,
			},
			expName: "testname",
			expType: DeviceTypeNetInterface,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expName, tc.dev.DeviceName(), "")
			test.AssertEqual(t, tc.expType, tc.dev.DeviceType(), "")
			test.AssertEqual(t, tc.dev, tc.dev.PCIDevice(), "")
		})
	}
}
