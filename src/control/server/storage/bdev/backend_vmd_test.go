//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"os/user"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestBackend_substituteVMDAddresses(t *testing.T) {
	const (
		vmdAddr         = "0000:5d:05.5"
		vmdBackingAddr1 = "5d0505:01:00.0"
		vmdBackingAddr2 = "5d0505:03:00.0"
	)

	for name, tc := range map[string]struct {
		inAddrs     *hardware.PCIAddressSet
		bdevCache   *storage.BdevScanResponse
		expOutAddrs *hardware.PCIAddressSet
		expErr      error
	}{
		"one vmd requested; no backing devices": {
			inAddrs: addrListFromStrings(vmdAddr),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs("850505:07:00.0", "850505:09:00.0",
					"850505:0b:00.0", "850505:0d:00.0", "850505:0f:00.0",
					"850505:11:00.0", "850505:14:00.0"),
			},
			expOutAddrs: addrListFromStrings(vmdAddr),
		},
		"one vmd requested; two backing devices": {
			inAddrs: addrListFromStrings(vmdAddr),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs(vmdBackingAddr1, vmdBackingAddr2),
			},
			expOutAddrs: addrListFromStrings(vmdBackingAddr1, vmdBackingAddr2),
		},
		"two vmds requested; one has backing devices": {
			inAddrs: addrListFromStrings(vmdAddr, "0000:85:05.5"),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs("850505:07:00.0", "850505:09:00.0",
					"850505:0b:00.0", "850505:0d:00.0", "850505:0f:00.0",
					"850505:11:00.0", "850505:14:00.0"),
			},
			expOutAddrs: addrListFromStrings(vmdAddr, "850505:07:00.0",
				"850505:09:00.0", "850505:0b:00.0", "850505:0d:00.0",
				"850505:0f:00.0", "850505:11:00.0", "850505:14:00.0"),
		},
		"two vmds requested; both have backing devices": {
			inAddrs: addrListFromStrings(vmdAddr, "0000:85:05.5"),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs(vmdBackingAddr1, vmdBackingAddr2,
					"850505:07:00.0", "850505:09:00.0", "850505:0b:00.0",
					"850505:0d:00.0", "850505:0f:00.0", "850505:11:00.0",
					"850505:14:00.0"),
			},
			expOutAddrs: addrListFromStrings(vmdBackingAddr1, vmdBackingAddr2,
				"850505:07:00.0", "850505:09:00.0", "850505:0b:00.0",
				"850505:0d:00.0", "850505:0f:00.0", "850505:11:00.0",
				"850505:14:00.0"),
		},
		"input vmd backing devices": {
			inAddrs: addrListFromStrings(vmdBackingAddr2, vmdBackingAddr1),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs(vmdBackingAddr1, vmdBackingAddr2,
					"850505:07:00.0", "850505:09:00.0", "850505:0b:00.0",
					"850505:0d:00.0", "850505:0f:00.0", "850505:11:00.0",
					"850505:14:00.0"),
			},
			expOutAddrs: addrListFromStrings(vmdBackingAddr1, vmdBackingAddr2),
		},
		"input vmd backing devices; no cache": {
			inAddrs:     addrListFromStrings(vmdBackingAddr2, vmdBackingAddr1),
			expOutAddrs: addrListFromStrings(vmdBackingAddr1, vmdBackingAddr2),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			gotAddrs, gotErr := substituteVMDAddresses(log, tc.inAddrs, tc.bdevCache)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutAddrs, gotAddrs, defCmpOpts()...); diff != "" {
				t.Fatalf("unexpected output addresses (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBackend_vmdFilterAddresses(t *testing.T) {
	testNrHugePages := 42
	usrCurrent, _ := user.Current()
	username := usrCurrent.Username

	for name, tc := range map[string]struct {
		inReq        *storage.BdevPrepareRequest
		inVmdAddrs   *hardware.PCIAddressSet
		expAllowList *hardware.PCIAddressSet
		expBlockList *hardware.PCIAddressSet
		expOutReq    *storage.BdevPrepareRequest
		expErr       error
	}{
		"no filters": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			inVmdAddrs:   mockAddrList(1, 2),
			expAllowList: mockAddrList(1, 2),
		},
		"addresses allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(1, 2),
			},
			inVmdAddrs:   mockAddrList(1, 2),
			expAllowList: mockAddrList(1, 2),
		},
		"addresses not allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(1, 2),
			},
			inVmdAddrs: mockAddrList(3, 4),
		},
		"addresses partially allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(1),
			},
			inVmdAddrs:   mockAddrList(3, 1),
			expAllowList: mockAddrList(1),
		},
		"addresses blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  mockAddrListStr(1, 2),
			},
			inVmdAddrs: mockAddrList(1, 2),
		},
		"addresses not blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  mockAddrListStr(1, 2),
			},
			inVmdAddrs:   mockAddrList(3, 4),
			expAllowList: mockAddrList(3, 4),
			expBlockList: mockAddrList(1, 2),
		},
		"addresses partially blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  mockAddrListStr(1),
			},
			inVmdAddrs:   mockAddrList(3, 1),
			expAllowList: mockAddrList(3),
		},
		"addresses partially allowed and partially blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(1, 2),
				PCIBlockList:  mockAddrListStr(1),
			},
			inVmdAddrs:   mockAddrList(3, 2, 1),
			expAllowList: mockAddrList(2),
		},
	} {
		t.Run(name, func(t *testing.T) {
			allowList, blockList, gotErr := vmdFilterAddresses(tc.inReq, tc.inVmdAddrs)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if tc.expAllowList == nil {
				tc.expAllowList = mockAddrList()
			}
			if tc.expBlockList == nil {
				tc.expBlockList = mockAddrList()
			}

			if diff := cmp.Diff(tc.expAllowList, allowList, defCmpOpts()...); diff != "" {
				t.Fatalf("unexpected output address list (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expBlockList, blockList, defCmpOpts()...); diff != "" {
				t.Fatalf("unexpected output address list (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBackend_updatePrepareRequest(t *testing.T) {
	testNrHugePages := 42
	usrCurrent, _ := user.Current()
	username := usrCurrent.Username

	for name, tc := range map[string]struct {
		inReq     *storage.BdevPrepareRequest
		detectVMD vmdDetectFn
		expOutReq *storage.BdevPrepareRequest
		expErr    error
	}{
		"vmd not enabled": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
		},
		"vmd enabled; vmd detection fails": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			detectVMD: func() (*hardware.PCIAddressSet, error) { return nil, errors.New("test") },
			expErr:    errors.New("test"),
		},
		"vmd enabled; no vmds detected": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			detectVMD: func() (*hardware.PCIAddressSet, error) { return nil, nil },
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
		},
		"vmd enabled; vmds detected": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			detectVMD: func() (*hardware.PCIAddressSet, error) {
				return mockAddrList(1, 2), nil
			},
			expOutReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(1, 2),
			},
		},
		"vmd enabled; vmds detected; some in allow list": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  test.MockPCIAddr(1),
			},
			detectVMD: func() (*hardware.PCIAddressSet, error) {
				return mockAddrList(1, 2), nil
			},
			expOutReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  test.MockPCIAddr(1),
			},
		},
		"vmd enabled; vmds detected; some in block list": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  test.MockPCIAddr(1),
			},
			detectVMD: func() (*hardware.PCIAddressSet, error) {
				return mockAddrList(1, 2), nil
			},
			expOutReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  test.MockPCIAddr(2),
			},
		},
		"vmd enabled; vmds detected; all in block list; vmd disabled": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  mockAddrListStr(1, 2),
			},
			detectVMD: func() (*hardware.PCIAddressSet, error) {
				return mockAddrList(1, 2), nil
			},
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  mockAddrListStr(1, 2),
			},
		},
		"vmd enabled; vmds detected; none in allow list; vmd disabled": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(1, 2),
			},
			detectVMD: func() (*hardware.PCIAddressSet, error) {
				return mockAddrList(3, 4), nil
			},
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: strings.Join([]string{
					test.MockPCIAddr(1), test.MockPCIAddr(2),
				}, " "),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			gotErr := updatePrepareRequest(log, tc.inReq, tc.detectVMD)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutReq, tc.inReq); diff != "" {
				t.Fatalf("unexpected output request (-want, +got):\n%s\n", diff)
			}
		})
	}
}
