//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"testing"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

func TestPCIUtils_NewPCIAddress(t *testing.T) {
	for name, tc := range map[string]struct {
		addrStr string
		expDom  string
		expBus  string
		expDev  string
		expFun  string
		expErr  error
	}{
		"valid": {
			addrStr: "0000:80:00.0",
			expDom:  "0000",
			expBus:  "80",
			expDev:  "00",
			expFun:  "0",
		},
		"invalid": {
			addrStr: "0000:gg:00.0",
			expErr:  errors.New("parsing \"gg\""),
		},
		"vmd address": {
			addrStr: "0000:5d:05.5",
			expDom:  "0000",
			expBus:  "5d",
			expDev:  "05",
			expFun:  "5",
		},
		"vmd backing device address": {
			addrStr: "5d0505:01:00.0",
			expDom:  "5d0505",
			expBus:  "01",
			expDev:  "00",
			expFun:  "0",
		},
	} {
		t.Run(name, func(t *testing.T) {
			addr, err := NewPCIAddress(tc.addrStr)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expDom, addr.Domain, "bad domain")
			AssertEqual(t, tc.expBus, addr.Bus, "bad bus")
			AssertEqual(t, tc.expDev, addr.Device, "bad device")
			AssertEqual(t, tc.expFun, addr.Function, "bad function")
		})
	}
}

func TestPCIUtils_NewPCIAddressSet(t *testing.T) {
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
		"invalid": {
			addrStrs: []string{"0000:7f.00.0"},
			expErr:   errors.New("bdf format"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			addrSet, err := NewPCIAddressSet(tc.addrStrs...)
			CmpErr(t, tc.expErr, err)
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

func TestPCIUtils_PCIAddressSet_Intersect(t *testing.T) {
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

func TestPCIUtils_PCIAddressSet_Difference(t *testing.T) {
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

func TestPCIUtils_PCIAddressSet_BackingToVMDAddresses(t *testing.T) {
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
		"multiple vmd address": {
			inAddrs:     []string{"5d0505:01:00.0", "5d0505:03:00.0"},
			expOutAddrs: []string{"0000:5d:05.5"},
		},
		"invalid vmd domain in address": {
			inAddrs: []string{"5d055:01:00.0"},
			expErr:  errors.New("unexpected length of vmd domain"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			addrSet, err := NewPCIAddressSet(tc.inAddrs...)
			if err != nil {
				t.Fatal(err)
			}

			gotAddrs, gotErr := addrSet.BackingToVMDAddresses(log)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutAddrs, gotAddrs.Strings()); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestCommon_GetRangeLimits(t *testing.T) {
	for name, tc := range map[string]struct {
		rangeStr string
		expBegin uint64
		expEnd   uint64
		expErr   error
	}{
		"hexadecimal": {
			rangeStr: "0x80-0x8f",
			expBegin: 0x80,
			expEnd:   0x8f,
		},
		"incorrect hexadecimal": {
			rangeStr: "0x8g-0x8f",
			expErr:   errors.New("parsing \"0x8g\""),
		},
		"hexadecimal upper": {
			rangeStr: "0x80-0x8F",
			expBegin: 0x80,
			expEnd:   0x8F,
		},
		"decimal": {
			rangeStr: "128-143",
			expBegin: 0x80,
			expEnd:   0x8F,
		},
		"bad range": {
			rangeStr: "128-143-0",
			expErr:   errors.New("invalid busid range \"128-143-0\""),
		},
		"reverse range": {
			rangeStr: "143-0",
			expErr:   errors.New("invalid busid range \"143-0\""),
		},
		"bad separator": {
			rangeStr: "143:0",
			expErr:   errors.New("invalid busid range \"143:0\""),
		},
	} {
		t.Run(name, func(t *testing.T) {
			begin, end, err := GetRangeLimits(tc.rangeStr)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expBegin, begin, "bad beginning limit")
			AssertEqual(t, tc.expEnd, end, "bad ending limit")
		})
	}
}
