//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
)

func TestServer_CtlSvc_checkCfgBdevs(t *testing.T) {
	scanAddrs := []string{
		"0000:90:00.0", "0000:d8:00.0", "5d0505:01:00.0", "0000:8e:00.0",
		"0000:8a:00.0", "0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0",
		"0000:8f:00.0", "5d0505:03:00.0",
	}
	scanCtrlrs := make(storage.NvmeControllers, len(scanAddrs))
	for idx, addr := range scanAddrs {
		scanCtrlrs[idx] = &storage.NvmeController{PciAddr: addr}
	}

	for name, tc := range map[string]struct {
		numIOSrvs       int
		vmdEnabled      bool
		inScanResp      *bdev.ScanResponse
		inCfgBdevLists  [][]string
		expCfgBdevLists [][]string
		expErr          error
	}{
		"vmd in scan but empty cfg bdev list": {
			inCfgBdevLists:  [][]string{{}},
			expCfgBdevLists: [][]string{{}},
		},
		"vmd in scan with addr in cfg bdev list but vmd disabled": {
			inCfgBdevLists: [][]string{{"0000:5d:05.5"}},
			expErr:         FaultBdevNotFound([]string{"0000:5d:05.5"}),
		},
		"vmd in scan with addr in cfg bdev list": {
			vmdEnabled:      true,
			inCfgBdevLists:  [][]string{{"0000:5d:05.5"}},
			expCfgBdevLists: [][]string{{"5d0505:01:00.0", "5d0505:03:00.0"}},
		},
		"vmd with no backing devices with addr in cfg bdev list": {
			vmdEnabled:     true,
			inCfgBdevLists: [][]string{{"0000:d7:05.5"}},
			expErr:         FaultBdevNotFound([]string{"0000:d7:05.5"}),
		},
		"vmd and non vmd with no backing devices with addr in cfg bdev list": {
			vmdEnabled:     true,
			inCfgBdevLists: [][]string{{"0000:8a:00.0", "0000:d7:05.5"}},
			expErr:         FaultBdevNotFound([]string{"0000:d7:05.5"}),
		},
		"vmd and non vmd in scan with addr in cfg bdev list": {
			vmdEnabled:     true,
			inCfgBdevLists: [][]string{{"0000:8a:00.0", "0000:8d:00.0", "0000:5d:05.5"}},
			expCfgBdevLists: [][]string{
				{"0000:8a:00.0", "0000:8d:00.0", "5d0505:01:00.0", "5d0505:03:00.0"},
			},
		},
		"vmd and non vmd in scan with addr in cfg bdev list on multiple io servers": {
			numIOSrvs:  2,
			vmdEnabled: true,
			inScanResp: &bdev.ScanResponse{
				Controllers: append(scanCtrlrs,
					&storage.NvmeController{PciAddr: "d70505:01:00.0"},
					&storage.NvmeController{PciAddr: "d70505:02:00.0"}),
			},
			inCfgBdevLists: [][]string{
				{"0000:90:00.0", "0000:d8:00.0", "0000:d7:05.5"},
				{"0000:8a:00.0", "0000:8d:00.0", "0000:5d:05.5"},
			},
			expCfgBdevLists: [][]string{
				{"0000:90:00.0", "0000:d8:00.0", "d70505:01:00.0", "d70505:02:00.0"},
				{"0000:8a:00.0", "0000:8d:00.0", "5d0505:01:00.0", "5d0505:03:00.0"},
			},
		},
		"missing ssd in cfg bdev list": {
			numIOSrvs:      2,
			inCfgBdevLists: [][]string{{"0000:90:00.0"}, {"0000:80:00.0"}},
			expErr:         FaultBdevNotFound([]string{"0000:80:00.0"}),
		},
		"present ssds in cfg bdev list": {
			numIOSrvs: 2,
			inCfgBdevLists: [][]string{
				{"0000:90:00.0", "0000:d8:00.0", "0000:8e:00.0", "0000:8a:00.0"},
				{"0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0", "0000:8f:00.0"},
			},
			expCfgBdevLists: [][]string{
				{"0000:90:00.0", "0000:d8:00.0", "0000:8e:00.0", "0000:8a:00.0"},
				{"0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0", "0000:8f:00.0"},
			},
		},
		"unexpected scan": {
			numIOSrvs: 2,
			inScanResp: &bdev.ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			inCfgBdevLists: [][]string{
				{"0000:90:00.0", "0000:d8:00.0", "0000:8e:00.0", "0000:8a:00.0"},
				{"0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0", "0000:8f:00.0"},
			},
			expErr: FaultBdevNotFound([]string{
				"0000:90:00.0", "0000:d8:00.0", "0000:8e:00.0", "0000:8a:00.0",
			}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			if tc.numIOSrvs == 0 {
				tc.numIOSrvs = 1
			}
			if len(tc.inCfgBdevLists) != tc.numIOSrvs {
				t.Fatal("test params: inCfgBdevLists length incorrect")
			}

			// set config device lists
			testCfg := config.DefaultServer()
			testCfg.Servers = make([]*ioserver.Config, tc.numIOSrvs)
			for idx := 0; idx < tc.numIOSrvs; idx++ {
				testCfg.Servers[idx] = ioserver.NewConfig().
					WithBdevClass("nvme").
					WithBdevDeviceList(tc.inCfgBdevLists[idx]...)
			}

			mbc := &bdev.MockBackendConfig{VmdEnabled: tc.vmdEnabled}
			cs := mockControlService(t, log, testCfg, mbc, nil, nil)

			if tc.inScanResp == nil {
				tc.inScanResp = &bdev.ScanResponse{
					Controllers: scanCtrlrs,
				}
			}
			gotErr := cs.checkCfgBdevs(tc.inScanResp)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if len(tc.expCfgBdevLists) != tc.numIOSrvs {
				t.Fatal("test params: expCfgBdevLists length incorrect")
			}

			for idx := 0; idx < tc.numIOSrvs; idx++ {
				cfgBdevs := cs.instanceStorage[idx].Bdev.GetNvmeDevs()
				diff := cmp.Diff(tc.expCfgBdevLists[idx], cfgBdevs)
				if diff != "" {
					t.Fatalf("unexpected device list (-want, +got):\n%s\n",
						diff)
				}
			}
		})
	}
}
