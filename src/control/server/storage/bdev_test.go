//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"encoding/json"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
)

func ctrlrsFromPCIAddrs(addrs ...string) NvmeControllers {
	ncs := make(NvmeControllers, len(addrs))
	for i, addr := range addrs {
		nc := NvmeController{PciAddr: addr}
		ncs[i] = &nc
	}
	return ncs
}

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

// Test_Convert_SmdDevice verifies proto->native and native->native JSON conversions.
func Test_Convert_SmdDevice(t *testing.T) {
	native := MockSmdDevice(test.MockPCIAddr(1))
	origTgts := native.TargetIDs
	// Validate target IDs get de-duplicated and HasSysXS set appropriately
	native.TargetIDs = append(native.TargetIDs, sysXSTgtID, native.TargetIDs[0])
	native.NvmeState = NvmeStateFaulty
	native.LedState = LedStateFaulty

	proto := new(ctlpb.SmdDevice)
	if err := convert.Types(native, proto); err != nil {
		t.Fatal(err)
	}

	test.AssertEqual(t, proto.Uuid, native.UUID, "uuid match")
	test.AssertEqual(t, proto.TgtIds, native.TargetIDs, "targets match")
	test.AssertEqual(t, NvmeDevState(proto.DevState), native.NvmeState, "nvme dev state match")
	test.AssertEqual(t, LedState(proto.LedState), native.LedState, "dev led state match")
	test.AssertEqual(t, OptionBits(proto.RoleBits), native.Roles.OptionBits, "roles match")

	convertedNative := new(SmdDevice)
	if err := convert.Types(proto, convertedNative); err != nil {
		t.Fatal(err)
	}

	// Validate target IDs get de-duplicated and HasSysXS set appropriately
	native.TargetIDs = origTgts
	native.HasSysXS = true
	if diff := cmp.Diff(native, convertedNative); diff != "" {
		t.Fatalf("expected converted device to match original (-want, +got):\n%s\n", diff)
	}

	newNative := new(SmdDevice)
	if err := convert.Types(native, newNative); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(native, newNative); diff != "" {
		t.Fatalf("expected new device to match original (-want, +got):\n%s\n", diff)
	}

	out, err := json.Marshal(newNative)
	if err != nil {
		t.Fatal(err)
	}
	expOut := `{"role_bits":7,"uuid":"00000001-0001-0001-0001-000000000001","tgt_ids":[5,6,7,8],` +
		`"dev_state":"EVICTED","led_state":"ON","rank":0,"total_bytes":0,"avail_bytes":0,` +
		`"usable_bytes":0,"cluster_size":0,"meta_size":0,"meta_wal_size":0,"rdb_size":0,` +
		`"rdb_wal_size":0,"health":null,"tr_addr":"0000:01:00.0","roles":"data,meta,wal",` +
		`"has_sys_xs":true}`
	if diff := cmp.Diff(expOut, string(out)); diff != "" {
		t.Fatalf("expected json output to be human readable (-want, +got):\n%s\n", diff)
	}
}

func Test_NvmeController_Update(t *testing.T) {
	mockCtrlrs := MockNvmeControllers(5)

	// Verify in-place update.
	test.AssertEqual(t, mockCtrlrs[1].SmdDevices[0].UUID, test.MockUUID(1), "unexpected uuid")
	c1 := MockNvmeController(1)
	c1.SmdDevices[0].UUID = test.MockUUID(10)
	mockCtrlrs.Update(*c1)
	test.AssertEqual(t, len(mockCtrlrs), 5, "expected 5")
	test.AssertEqual(t, mockCtrlrs[1].SmdDevices[0].UUID, test.MockUUID(10), "unexpected uuid")

	// Verify multiple new controllers are added.
	c2 := MockNvmeController(6)
	c3 := MockNvmeController(9)
	mockCtrlrs.Update(*c2, *c3)
	test.AssertEqual(t, len(mockCtrlrs), 7, "expected 7")
}

func Test_NvmeController_Addresses(t *testing.T) {
	for name, tc := range map[string]struct {
		ctrlrs   NvmeControllers
		expAddrs []string
		expErr   error
	}{
		"two vmd endpoints with two backing devices": {
			ctrlrs: ctrlrsFromPCIAddrs(
				"5d0505:03:00.0",
				"7d0505:01:00.0",
				"5d0505:01:00.0",
				"7d0505:03:00.0",
			),
			expAddrs: []string{
				"5d0505:01:00.0",
				"5d0505:03:00.0",
				"7d0505:01:00.0",
				"7d0505:03:00.0",
			},
		},
		"no addresses": {
			expAddrs: []string{},
		},
		"invalid address": {
			ctrlrs: ctrlrsFromPCIAddrs("a"),
			expErr: errors.New("unable to parse"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotAddrs, gotErr := tc.ctrlrs.Addresses()
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			gotAddrStrs := gotAddrs.Strings()
			t.Logf("ea: %v\n", gotAddrStrs)

			if diff := cmp.Diff(tc.expAddrs, gotAddrStrs); diff != "" {
				//if diff := cmp.Diff(tc.expAddrs, gotAddrs.Strings()); diff != "" {
				t.Fatalf("unexpected output address set (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func Test_filterBdevScanResponse(t *testing.T) {
	for name, tc := range map[string]struct {
		addrs    []string
		scanResp *BdevScanResponse
		expAddrs []string
		expErr   error
	}{
		"two vmd endpoints; one filtered out": {
			addrs: []string{"0000:7d:05.5"},
			scanResp: &BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs(
					"5d0505:03:00.0",
					"7d0505:01:00.0",
					"5d0505:01:00.0",
					"7d0505:03:00.0",
				),
			},
			expAddrs: []string{
				"7d0505:01:00.0",
				"7d0505:03:00.0",
			},
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
			gotResp, gotErr := filterBdevScanResponse(bdl, tc.scanResp)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			expAddrStr := strings.Join(tc.expAddrs, ", ")
			if diff := cmp.Diff(expAddrStr, gotResp.Controllers.String()); diff != "" {
				t.Fatalf("unexpected output addresses (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func Test_CalcMinHugepages(t *testing.T) {
	for name, tc := range map[string]struct {
		input      *common.MemInfo
		numTargets int
		expPages   int
		expErr     error
	}{
		"no pages": {
			input:      &common.MemInfo{},
			numTargets: 1,
			expErr:     errors.New("invalid system hugepage size"),
		},
		"no targets": {
			input: &common.MemInfo{
				HugepageSizeKiB: 2048,
			},
			expErr: errors.New("numTargets"),
		},
		"2KB pagesize; 16 targets": {
			input: &common.MemInfo{
				HugepageSizeKiB: 2048,
			},
			numTargets: 16,
			expPages:   8192,
		},
		"2KB pagesize; 31 targets": {
			input: &common.MemInfo{
				HugepageSizeKiB: 2048,
			},
			numTargets: 31,
			expPages:   15872,
		},
		"1GB pagesize; 16 targets": {
			input: &common.MemInfo{
				HugepageSizeKiB: 1048576,
			},
			numTargets: 16,
			expPages:   16,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotPages, gotErr := CalcMinHugepages(tc.input.HugepageSizeKiB, tc.numTargets)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if gotPages != tc.expPages {
				t.Fatalf("expected %d, got %d", tc.expPages, gotPages)
			}
		})
	}
}
