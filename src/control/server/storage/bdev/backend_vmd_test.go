//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
	"os/user"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func addrListFromStrings(t *testing.T, addrs ...string) *common.PCIAddressSet {
	al, err := common.NewPCIAddressSet(addrs...)
	if err != nil {
		t.Fatal(err)
	}

	return al
}

func mockAddrList(t *testing.T, idxs ...int) *common.PCIAddressSet {
	t.Helper()
	var addrs []string

	for _, idx := range idxs {
		addrs = append(addrs, common.MockPCIAddr(int32(idx)))
	}

	return addrListFromStrings(t, addrs...)
}

func mockAddrListStr(t *testing.T, idxs ...int) string {
	t.Helper()
	return mockAddrList(t, idxs...).String()
}

func TestBackend_substituteVMDAddresses(t *testing.T) {
	const (
		vmdAddr         = "0000:5d:05.5"
		vmdBackingAddr1 = "5d0505:01:00.0"
		vmdBackingAddr2 = "5d0505:03:00.0"
	)

	for name, tc := range map[string]struct {
		inAddrs     *common.PCIAddressSet
		bdevCache   *storage.BdevScanResponse
		expOutAddrs *common.PCIAddressSet
		expErr      error
	}{
		"one vmd requested; no backing devices": {
			inAddrs: addrListFromStrings(t, vmdAddr),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs("850505:07:00.0", "850505:09:00.0",
					"850505:0b:00.0", "850505:0d:00.0", "850505:0f:00.0",
					"850505:11:00.0", "850505:14:00.0"),
			},
			expOutAddrs: addrListFromStrings(t, vmdAddr),
		},
		"one vmd requested; two backing devices": {
			inAddrs: addrListFromStrings(t, vmdAddr),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs(vmdBackingAddr1, vmdBackingAddr2),
			},
			expOutAddrs: addrListFromStrings(t, vmdBackingAddr1, vmdBackingAddr2),
		},
		"two vmds requested; one has backing devices": {
			inAddrs: addrListFromStrings(t, vmdAddr, "0000:85:05.5"),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs("850505:07:00.0", "850505:09:00.0",
					"850505:0b:00.0", "850505:0d:00.0", "850505:0f:00.0",
					"850505:11:00.0", "850505:14:00.0"),
			},
			expOutAddrs: addrListFromStrings(t,
				vmdAddr, "850505:07:00.0", "850505:09:00.0", "850505:0b:00.0",
				"850505:0d:00.0", "850505:0f:00.0", "850505:11:00.0",
				"850505:14:00.0"),
		},
		"two vmds requested; both have backing devices": {
			inAddrs: addrListFromStrings(t, vmdAddr, "0000:85:05.5"),
			bdevCache: &storage.BdevScanResponse{
				Controllers: ctrlrsFromPCIAddrs(vmdBackingAddr1, vmdBackingAddr2,
					"850505:07:00.0", "850505:09:00.0", "850505:0b:00.0",
					"850505:0d:00.0", "850505:0f:00.0", "850505:11:00.0",
					"850505:14:00.0"),
			},
			expOutAddrs: addrListFromStrings(t,
				vmdBackingAddr1, vmdBackingAddr2, "850505:07:00.0",
				"850505:09:00.0", "850505:0b:00.0", "850505:0d:00.0",
				"850505:0f:00.0", "850505:11:00.0", "850505:14:00.0"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			gotAddrs, gotErr := substituteVMDAddresses(log, tc.inAddrs, tc.bdevCache)
			common.CmpErr(t, tc.expErr, gotErr)
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
		inReq      *storage.BdevPrepareRequest
		inVmdAddrs *common.PCIAddressSet
		expOutReq  *storage.BdevPrepareRequest
		expErr     error
	}{
		"no filters": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			inVmdAddrs: mockAddrList(t, 1, 2),
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 1, 2),
			},
		},
		"addresses allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 1, 2),
			},
			inVmdAddrs: mockAddrList(t, 1, 2),
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 1, 2),
			},
		},
		"addresses not allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 1, 2),
			},
			inVmdAddrs: mockAddrList(t, 3, 4),
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
		},
		"addresses partially allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 1),
			},
			inVmdAddrs: mockAddrList(t, 3, 1),
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 1),
			},
		},
		"addresses blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  mockAddrListStr(t, 1, 2),
			},
			inVmdAddrs: mockAddrList(t, 1, 2),
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
		},
		"addresses not blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  mockAddrListStr(t, 1, 2),
			},
			inVmdAddrs: mockAddrList(t, 3, 4),
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 3, 4),
			},
		},
		"addresses partially blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  mockAddrListStr(t, 1),
			},
			inVmdAddrs: mockAddrList(t, 3, 1),
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 3),
			},
		},
		"addresses partially allowed and partially blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 1, 2),
				PCIBlockList:  mockAddrListStr(t, 1),
			},
			inVmdAddrs: mockAddrList(t, 3, 2, 1),
			expOutReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  mockAddrListStr(t, 2),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			outReq, gotErr := vmdFilterAddresses(tc.inReq, tc.inVmdAddrs)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutReq, outReq); diff != "" {
				t.Fatalf("unexpected output request (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBackend_getVMDPrepReq(t *testing.T) {
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
		},
		"vmd enabled; vmd detection fails": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			detectVMD: func() (*common.PCIAddressSet, error) { return nil, errors.New("test") },
			expErr:    errors.New("test"),
		},
		"vmd enabled; no vmds detected": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			detectVMD: func() (*common.PCIAddressSet, error) { return nil, nil },
		},
		"vmd enabled; vmds detected": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			detectVMD: func() (*common.PCIAddressSet, error) {
				al, _ := common.NewPCIAddressSet(common.MockPCIAddr(1), common.MockPCIAddr(2))
				return al, nil
			},
			expOutReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
					storage.BdevPciAddrSep, common.MockPCIAddr(2)),
			},
		},
		"vmd enabled; vmds detected; in allow list": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  common.MockPCIAddr(1),
			},
			detectVMD: func() (*common.PCIAddressSet, error) {
				al, _ := common.NewPCIAddressSet(common.MockPCIAddr(1), common.MockPCIAddr(2))
				return al, nil
			},
			expOutReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  common.MockPCIAddr(1),
			},
		},
		"vmd enabled; vmds detected; in block list": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  common.MockPCIAddr(1),
			},
			detectVMD: func() (*common.PCIAddressSet, error) {
				al, _ := common.NewPCIAddressSet(common.MockPCIAddr(1), common.MockPCIAddr(2))
				return al, nil
			},
			expOutReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  common.MockPCIAddr(2),
			},
		},
		"vmd enabled; vmds detected; all in block list": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList: strings.Join([]string{
					common.MockPCIAddr(1), common.MockPCIAddr(2),
				}, " "),
			},
			detectVMD: func() (*common.PCIAddressSet, error) {
				al, _ := common.NewPCIAddressSet(common.MockPCIAddr(1), common.MockPCIAddr(2))
				return al, nil
			},
		},
		"vmd enabled; vmds detected; none in allow list": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: strings.Join([]string{
					common.MockPCIAddr(1), common.MockPCIAddr(2),
				}, " "),
			},
			detectVMD: func() (*common.PCIAddressSet, error) {
				al, _ := common.NewPCIAddressSet(common.MockPCIAddr(3), common.MockPCIAddr(4))
				return al, nil
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			gotReq, gotErr := getVMDPrepReq(log, tc.inReq, tc.detectVMD)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutReq, gotReq); diff != "" {
				t.Fatalf("unexpected output request (-want, +got):\n%s\n", diff)
			}
		})
	}
}
