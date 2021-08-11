//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package bdev

import (
	"fmt"
	"os/user"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
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
