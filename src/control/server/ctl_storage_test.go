//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestServer_CtlSvc_getScmUsage(t *testing.T) {
	mockScmNs0 := storage.MockScmNamespace(0)
	mockScmMountPath0 := "/mnt/daos0"
	mockScmNs0wMount := storage.MockScmNamespace(0)
	mockScmNs0wMount.Mount = storage.MockScmMountPoint(0)
	mockScmNs1 := storage.MockScmNamespace(1)
	mockScmMountPath1 := "/mnt/daos1"
	mockScmNs1wMount := storage.MockScmNamespace(1)
	mockScmNs1wMount.Mount = storage.MockScmMountPoint(1)

	for name, tc := range map[string]struct {
		smsc        *scm.MockSysConfig
		inResp      *storage.ScmScanResponse
		storageCfgs []storage.TierConfigs
		expErr      error
		expOutResp  *storage.ScmScanResponse
	}{
		"nil input response": {
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("response is nil"),
		},
		"no scm tier": {
			inResp: new(storage.ScmScanResponse),
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("expected exactly 1 SCM tier"),
		},
		"no namespaces in input response": {
			inResp: new(storage.ScmScanResponse),
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("missing namespaces"),
		},
		"no namespace matching pmem blockdevice in scan response": {
			inResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("no pmem namespace"),
		},
		"get usage fails": {
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{Err: errors.New("unknown")},
				},
			},
			inResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0,
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expErr: errors.New("unknown"),
		},
		"get usage": {
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{
						Total: mockScmNs0wMount.Mount.TotalBytes,
						Avail: mockScmNs0wMount.Mount.AvailBytes,
					},
				},
			},
			inResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0,
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
			},
			expOutResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0wMount,
				},
			},
		},
		"get usage; multiple engines": {
			smsc: &scm.MockSysConfig{
				GetfsUsageResps: []scm.GetfsUsageRetval{
					{
						Total: mockScmNs0wMount.Mount.TotalBytes,
						Avail: mockScmNs0wMount.Mount.AvailBytes,
					},
					{
						Total: mockScmNs1wMount.Mount.TotalBytes,
						Avail: mockScmNs1wMount.Mount.AvailBytes,
					},
				},
			},
			inResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0,
					mockScmNs1,
				},
			},
			storageCfgs: []storage.TierConfigs{
				{
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath0).
						WithScmDeviceList(mockScmNs0.BlockDevice),
				},
				{
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint(mockScmMountPath1).
						WithScmDeviceList(mockScmNs1.BlockDevice),
				},
			},
			expOutResp: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{
					mockScmNs0wMount,
					mockScmNs1wMount,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			var engineCfgs []*engine.Config
			for _, sc := range tc.storageCfgs {
				engineCfgs = append(engineCfgs, engine.NewConfig().WithStorage(sc...))
			}
			sCfg := config.DefaultServer().WithEngines(engineCfgs...)
			cs := mockControlService(t, log, sCfg, nil, nil, tc.smsc)

			cs.harness.started.SetTrue()

			outResp, err := cs.getScmUsage(tc.inResp)
			common.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expOutResp, outResp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

// TODO DAOS-8040: re-enable VMD
// func TestServer_CtlSvc_checkCfgBdevs(t *testing.T) {
// 	scanAddrs := []string{
// 		"0000:90:00.0", "0000:d8:00.0", "5d0505:01:00.0", "0000:8e:00.0",
// 		"0000:8a:00.0", "0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0",
// 		"0000:8f:00.0", "5d0505:03:00.0",
// 	}
// 	scanCtrlrs := make(storage.NvmeControllers, len(scanAddrs))
// 	for idx, addr := range scanAddrs {
// 		scanCtrlrs[idx] = &storage.NvmeController{PciAddr: addr}
// 	}
//
// 	for name, tc := range map[string]struct {
// 		numEngines      int
// 		vmdEnabled      bool
// 		inScanResp      *bdev.ScanResponse
// 		inCfgBdevLists  [][]string
// 		expCfgBdevLists [][]string
// 		expErr          error
// 	}{
// 		"vmd in scan but empty cfg bdev list": {
// 			inCfgBdevLists:  [][]string{{}},
// 			expCfgBdevLists: [][]string{{}},
// 		},
// 		"vmd in scan with addr in cfg bdev list but vmd disabled": {
// 			inCfgBdevLists: [][]string{{"0000:5d:05.5"}},
// 			expErr:         FaultBdevNotFound([]string{"0000:5d:05.5"}),
// 		},
// 		"vmd in scan with addr in cfg bdev list": {
// 			vmdEnabled:      true,
// 			inCfgBdevLists:  [][]string{{"0000:5d:05.5"}},
// 			expCfgBdevLists: [][]string{{"5d0505:01:00.0", "5d0505:03:00.0"}},
// 		},
// 		"vmd with no backing devices with addr in cfg bdev list": {
// 			vmdEnabled:     true,
// 			inCfgBdevLists: [][]string{{"0000:d7:05.5"}},
// 			expErr:         FaultBdevNotFound([]string{"0000:d7:05.5"}),
// 		},
// 		"vmd and non vmd with no backing devices with addr in cfg bdev list": {
// 			vmdEnabled:     true,
// 			inCfgBdevLists: [][]string{{"0000:8a:00.0", "0000:d7:05.5"}},
// 			expErr:         FaultBdevNotFound([]string{"0000:d7:05.5"}),
// 		},
// 		"vmd and non vmd in scan with addr in cfg bdev list": {
// 			vmdEnabled:     true,
// 			inCfgBdevLists: [][]string{{"0000:8a:00.0", "0000:8d:00.0", "0000:5d:05.5"}},
// 			expCfgBdevLists: [][]string{
// 				{"0000:8a:00.0", "0000:8d:00.0", "5d0505:01:00.0", "5d0505:03:00.0"},
// 			},
// 		},
// 		"vmd and non vmd in scan with addr in cfg bdev list on multiple io servers": {
// 			numEngines: 2,
// 			vmdEnabled: true,
// 			inScanResp: &bdev.ScanResponse{
// 				Controllers: append(scanCtrlrs,
// 					&storage.NvmeController{PciAddr: "d70505:01:00.0"},
// 					&storage.NvmeController{PciAddr: "d70505:02:00.0"}),
// 			},
// 			inCfgBdevLists: [][]string{
// 				{"0000:90:00.0", "0000:d8:00.0", "0000:d7:05.5"},
// 				{"0000:8a:00.0", "0000:8d:00.0", "0000:5d:05.5"},
// 			},
// 			expCfgBdevLists: [][]string{
// 				{"0000:90:00.0", "0000:d8:00.0", "d70505:01:00.0", "d70505:02:00.0"},
// 				{"0000:8a:00.0", "0000:8d:00.0", "5d0505:01:00.0", "5d0505:03:00.0"},
// 			},
// 		},
// 		"missing ssd in cfg bdev list": {
// 			numEngines:     2,
// 			inCfgBdevLists: [][]string{{"0000:90:00.0"}, {"0000:80:00.0"}},
// 			expErr:         FaultBdevNotFound([]string{"0000:80:00.0"}),
// 		},
// 		"present ssds in cfg bdev list": {
// 			numEngines: 2,
// 			inCfgBdevLists: [][]string{
// 				{"0000:90:00.0", "0000:d8:00.0", "0000:8e:00.0", "0000:8a:00.0"},
// 				{"0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0", "0000:8f:00.0"},
// 			},
// 			expCfgBdevLists: [][]string{
// 				{"0000:90:00.0", "0000:d8:00.0", "0000:8e:00.0", "0000:8a:00.0"},
// 				{"0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0", "0000:8f:00.0"},
// 			},
// 		},
// 		"unexpected scan": {
// 			numEngines: 2,
// 			inScanResp: &bdev.ScanResponse{
// 				Controllers: storage.MockNvmeControllers(3),
// 			},
// 			inCfgBdevLists: [][]string{
// 				{"0000:90:00.0", "0000:d8:00.0", "0000:8e:00.0", "0000:8a:00.0"},
// 				{"0000:8d:00.0", "0000:8b:00.0", "0000:8c:00.0", "0000:8f:00.0"},
// 			},
// 			expErr: FaultBdevNotFound([]string{
// 				"0000:90:00.0", "0000:d8:00.0", "0000:8e:00.0", "0000:8a:00.0",
// 			}),
// 		},
// 	} {
// 		t.Run(name, func(t *testing.T) {
// 			log, buf := logging.NewTestLogger(t.Name())
// 			defer common.ShowBufferOnFailure(t, buf)
//
// 			if tc.numEngines == 0 {
// 				tc.numEngines = 1
// 			}
// 			if len(tc.inCfgBdevLists) != tc.numEngines {
// 				t.Fatal("test params: inCfgBdevLists length incorrect")
// 			}
//
// 			// set config device lists
// 			testCfg := config.DefaultServer()
// 			testCfg.Engines = make([]*engine.Config, tc.numEngines)
// 			for idx := 0; idx < tc.numEngines; idx++ {
// 				testCfg.Engines[idx] = engine.NewConfig().
// 					WithBdevClass("nvme").
// 					WithBdevDeviceList(tc.inCfgBdevLists[idx]...)
// 			}
//
// 			mbc := &bdev.MockBackendConfig{VmdEnabled: tc.vmdEnabled}
// 			cs := mockControlService(t, log, testCfg, mbc, nil, nil)
//
// 			if tc.inScanResp == nil {
// 				tc.inScanResp = &bdev.ScanResponse{
// 					Controllers: scanCtrlrs,
// 				}
// 			}
// 			gotErr := cs.checkCfgBdevs(tc.inScanResp)
// 			common.CmpErr(t, tc.expErr, gotErr)
// 			if tc.expErr != nil {
// 				return
// 			}
//
// 			if len(tc.expCfgBdevLists) != tc.numEngines {
// 				t.Fatal("test params: expCfgBdevLists length incorrect")
// 			}
//
// 			for idx := 0; idx < tc.numEngines; idx++ {
// 				cfgBdevs := cs.instanceStorage[idx].Bdev.GetNvmeDevs()
// 				diff := cmp.Diff(tc.expCfgBdevLists[idx], cfgBdevs)
// 				if diff != "" {
// 					t.Fatalf("unexpected device list (-want, +got):\n%s\n",
// 						diff)
// 				}
// 			}
// 		})
// 	}
// }
