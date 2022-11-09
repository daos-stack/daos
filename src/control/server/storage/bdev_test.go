//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
)

func Test_NvmeDevState(t *testing.T) {
	for name, tc := range map[string]struct {
		state  NvmeDevState
		expStr string
		expErr error
	}{
		"new state": {
			state:  NvmeStateNew,
			expStr: "NEW",
		},
		"normal state": {
			state:  NvmeStateNormal,
			expStr: "NORMAL",
		},
		"faulty state": {
			state:  NvmeStateFaulty,
			expStr: "EVICTED",
		},
		"invalid state": {
			state:  NvmeDevState(99),
			expErr: errors.New("invalid nvme dev state 99"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			spb := new(ctlpb.NvmeDevState)
			gotErr := convert.Types(tc.state, spb)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			ns := new(NvmeDevState)
			if err := convert.Types(spb, ns); err != nil {
				t.Fatal(err)
			}

			test.AssertEqual(t, tc.state, *ns, "unexpected conversion result")
			test.AssertEqual(t, tc.expStr, ns.String(), "unexpected status string")
		})
	}
}

func Test_LedState(t *testing.T) {
	for name, tc := range map[string]struct {
		state  LedState
		expStr string
		expErr error
	}{
		"normal state": {
			state:  LedStateNormal,
			expStr: "OFF",
		},
		"identify state": {
			state:  LedStateIdentify,
			expStr: "QUICK_BLINK",
		},
		"faulty state": {
			state:  LedStateFaulty,
			expStr: "ON",
		},
		"rebuild state": {
			state:  LedStateRebuild,
			expStr: "SLOW_BLINK",
		},
		"unsupported state": {
			state:  LedStateUnknown,
			expStr: "NA",
		},
		"unexpected state": {
			state:  LedState(99),
			expErr: errors.New("invalid vmd led state 99"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			spb := new(ctlpb.LedState)
			gotErr := convert.Types(tc.state, spb)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			ns := new(LedState)
			if err := convert.Types(spb, ns); err != nil {
				t.Fatal(err)
			}

			test.AssertEqual(t, tc.state, *ns, "unexpected conversion result")
			test.AssertEqual(t, tc.expStr, ns.String(), "unexpected status string")
		})
	}
}

func Test_Convert_SmdDevice(t *testing.T) {
	native := MockSmdDevice(test.MockPCIAddr(1))

	s := new(ctlpb.SmdDevice)
	if err := convert.Types(native, s); err != nil {
		t.Fatal(err)
	}

	convertedNative := new(SmdDevice)
	if err := convert.Types(s, convertedNative); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(native, convertedNative); diff != "" {
		t.Fatalf("expected converted device to match original (-want, +got):\n%s\n", diff)
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
