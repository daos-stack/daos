//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
)

func Test_NvmeDevState(t *testing.T) {
	for name, tc := range map[string]struct {
		state       NvmeDevState
		expIsNew    bool
		expIsNormal bool
		expIsFaulty bool
		expStr      string
	}{
		"new state": {
			state:    NvmeStatePlugged,
			expIsNew: true,
			expStr:   "NEW",
		},
		"normal state": {
			state:       NvmeStatePlugged | NvmeStateInUse,
			expIsNormal: true,
			expStr:      "NORMAL",
		},
		"faulty state": {
			state:       NvmeStatePlugged | NvmeStateInUse | NvmeStateFaulty,
			expIsFaulty: true,
			expStr:      "EVICTED",
		},
		"new and faulty state": {
			state:       NvmeStatePlugged | NvmeStateFaulty,
			expIsNew:    true,
			expIsFaulty: true,
			expStr:      "EVICTED",
		},
		"unplugged, new and faulty": {
			state:  NvmeStateFaulty,
			expStr: "UNPLUGGED",
		},
		"new and identify state": {
			state:    NvmeStatePlugged | NvmeStateIdentify,
			expIsNew: true,
			expStr:   "NEW|IDENTIFY",
		},
		"new, faulty and identify state": {
			state:       NvmeStatePlugged | NvmeStateFaulty | NvmeStateIdentify,
			expIsNew:    true,
			expIsFaulty: true,
			expStr:      "EVICTED|IDENTIFY",
		},
		"normal and identify state": {
			state:       NvmeStatePlugged | NvmeStateInUse | NvmeStateIdentify,
			expIsNormal: true,
			expStr:      "NORMAL|IDENTIFY",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expIsNew, tc.state.IsNew(),
				"unexpected IsNew() result")
			test.AssertEqual(t, tc.expIsNormal, tc.state.IsNormal(),
				"unexpected IsNormal() result")
			test.AssertEqual(t, tc.expIsFaulty, tc.state.IsFaulty(),
				"unexpected IsFaulty() result")
			test.AssertEqual(t, tc.expStr, tc.state.String(),
				"unexpected status string")

			stateNew := NvmeDevStateFromString(tc.state.String())

			test.AssertEqual(t, tc.state.String(), stateNew.String(),
				fmt.Sprintf("expected string %s to yield state %s",
					tc.state.String(), stateNew.String()))
		})
	}
}

func Test_NvmeDevStateFromString_invalid(t *testing.T) {
	for name, tc := range map[string]struct {
		inStr    string
		expState NvmeDevState
		expStr   string
	}{
		"empty string": {
			expState: NvmeStateUnknown,
			expStr:   "UNKNOWN",
		},
		"unrecognized string": {
			inStr:    "BAD",
			expState: NvmeStateUnknown,
			expStr:   "UNKNOWN",
		},
	} {
		t.Run(name, func(t *testing.T) {
			state := NvmeDevStateFromString(tc.inStr)

			test.AssertEqual(t, tc.expState, state, "unexpected state")
			test.AssertEqual(t, tc.expStr, state.String(), "unexpected states string")
		})
	}
}

func Test_filterBdevScanResponse(t *testing.T) {
	const (
		vmdAddr1         = "0000:5d:05.5"
		vmdBackingAddr1a = "5d0505:01:00.0"
		vmdBackingAddr1b = "5d0505:03:00.0"
		vmdAddr2         = "0000:7d:05.5"
		vmdBackingAddr2a = "7d0505:01:00.0"
		vmdBackingAddr2b = "7d0505:03:00.0"
	)
	ctrlrsFromPCIAddrs := func(addrs ...string) (ncs NvmeControllers) {
		for _, addr := range addrs {
			ncs = append(ncs, &NvmeController{PciAddr: addr})
		}
		return
	}

	for name, tc := range map[string]struct {
		addrs    []string
		scanResp *BdevScanResponse
		expAddrs []string
		expErr   error
	}{
		"two vmd endpoints; one filtered out": {
			addrs: []string{vmdAddr2},
			scanResp: &BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs(vmdBackingAddr1a, vmdBackingAddr1b,
					vmdBackingAddr2a, vmdBackingAddr2b),
			},
			expAddrs: []string{vmdBackingAddr2a, vmdBackingAddr2b},
		},
		"two ssds; one filtered out": {
			addrs: []string{"0000:81:00.0"},
			scanResp: &BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs("0000:81:00.0", "0000:de:00.0"),
			},
			expAddrs: []string{"0000:81:00.0"},
		},
		"two aio kdev paths; both filtered out": {
			addrs: []string{"/dev/sda"},
			scanResp: &BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs("/dev/sda", "/dev/sdb"),
			},
			expAddrs: []string{},
		},
		"bad address; filtered out": {
			addrs: []string{"0000:81:00.0"},
			scanResp: &BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs("0000:81.00.0"),
			},
			expAddrs: []string{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			bdl := new(BdevDeviceList)
			if err := bdl.fromStrings(tc.addrs); err != nil {
				t.Fatal(err)
			}
			gotErr := filterBdevScanResponse(bdl, tc.scanResp)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			expAddrStr := strings.Join(tc.expAddrs, ", ")
			if diff := cmp.Diff(expAddrStr, tc.scanResp.Controllers.String()); diff != "" {
				t.Fatalf("unexpected output addresses (-want, +got):\n%s\n", diff)
			}
		})
	}
}
