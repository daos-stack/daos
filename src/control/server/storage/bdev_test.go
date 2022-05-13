//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/google/go-cmp/cmp"
)

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
			gotErr := filterBdevScanResponse(tc.addrs, tc.scanResp)
			common.CmpErr(t, tc.expErr, gotErr)
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
