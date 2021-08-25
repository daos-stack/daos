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

func TestBackend_vmdProcessFilters(t *testing.T) {
	testNrHugePages := 42
	usrCurrent, _ := user.Current()
	username := usrCurrent.Username

	for name, tc := range map[string]struct {
		inReq      *storage.BdevPrepareRequest
		inVmdAddrs []string
		expOutReq  storage.BdevPrepareRequest
	}{
		"no filters": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			inVmdAddrs: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			expOutReq: storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
					storage.BdevPciAddrSep, common.MockPCIAddr(2)),
			},
		},
		"addresses allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
					storage.BdevPciAddrSep, common.MockPCIAddr(2)),
			},
			inVmdAddrs: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			expOutReq: storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
					storage.BdevPciAddrSep, common.MockPCIAddr(2)),
			},
		},
		"addresses not allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
					storage.BdevPciAddrSep, common.MockPCIAddr(2)),
			},
			inVmdAddrs: []string{common.MockPCIAddr(3), common.MockPCIAddr(4)},
			expOutReq: storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
		},
		"addresses partially allowed": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  common.MockPCIAddr(1),
			},
			inVmdAddrs: []string{common.MockPCIAddr(3), common.MockPCIAddr(1)},
			expOutReq: storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  common.MockPCIAddr(1),
			},
		},
		"addresses blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
					storage.BdevPciAddrSep, common.MockPCIAddr(2)),
			},
			inVmdAddrs: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			expOutReq: storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
		},
		"addresses not blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
					storage.BdevPciAddrSep, common.MockPCIAddr(2)),
			},
			inVmdAddrs: []string{common.MockPCIAddr(3), common.MockPCIAddr(4)},
			expOutReq: storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(3),
					storage.BdevPciAddrSep, common.MockPCIAddr(4)),
			},
		},
		"addresses partially blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIBlockList:  common.MockPCIAddr(1),
			},
			inVmdAddrs: []string{common.MockPCIAddr(3), common.MockPCIAddr(1)},
			expOutReq: storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  common.MockPCIAddr(3),
			},
		},
		"addresses partially allowed and partially blocked": {
			inReq: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
					storage.BdevPciAddrSep, common.MockPCIAddr(2)),
				PCIBlockList: common.MockPCIAddr(1),
			},
			inVmdAddrs: []string{common.MockPCIAddr(3), common.MockPCIAddr(2), common.MockPCIAddr(1)},
			expOutReq: storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList:  common.MockPCIAddr(2),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			outReq := vmdProcessFilters(tc.inReq, tc.inVmdAddrs)
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
			detectVMD: func() ([]string, error) { return nil, errors.New("test") },
			expErr:    errors.New("test"),
		},
		"vmd enabled; no vmds detected": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			detectVMD: func() ([]string, error) { return nil, nil },
		},
		"vmd enabled; vmds detected": {
			inReq: &storage.BdevPrepareRequest{
				EnableVMD:     true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
			},
			detectVMD: func() ([]string, error) {
				return []string{common.MockPCIAddr(1), common.MockPCIAddr(2)}, nil
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
			detectVMD: func() ([]string, error) {
				return []string{common.MockPCIAddr(1), common.MockPCIAddr(2)}, nil
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
			detectVMD: func() ([]string, error) {
				return []string{common.MockPCIAddr(1), common.MockPCIAddr(2)}, nil
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
			detectVMD: func() ([]string, error) {
				return []string{common.MockPCIAddr(1), common.MockPCIAddr(2)}, nil
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
			detectVMD: func() ([]string, error) {
				return []string{common.MockPCIAddr(3), common.MockPCIAddr(4)}, nil
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
