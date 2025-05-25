//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os/user"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
)

var (
	defaultMultiAddrList  = fmt.Sprintf("%s%s%s", test.MockPCIAddr(1), cliPCIAddrSep, test.MockPCIAddr(2))
	defaultSingleAddrList = test.MockPCIAddr(1)
	spaceSepMultiAddrList = fmt.Sprintf("%s%s%s", test.MockPCIAddr(1), storage.BdevPciAddrSep, test.MockPCIAddr(2))
	currentUsername       string
	defPrepCmpOpt         = cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
		if x == nil && y == nil {
			return true
		}
		return x.Equals(y)
	})
	defSysMemInfo = func() *common.SysMemInfo {
		return control.MockSysMemInfo()
	}
	defHugeNodesStr = fmt.Sprintf("nodes_hp[0]=%d", defSysMemInfo().HugepagesTotal)
)

func getCurrentUsername(t *testing.T) string {
	t.Helper()

	if currentUsername == "" {
		usrCurrent, err := user.Current()
		if err != nil {
			t.Fatal(err)
		}
		currentUsername = usrCurrent.Username
	}

	return currentUsername
}

func getMockNvmeCmdInit(log logging.Logger, bmbc bdev.MockBackendConfig, sc *config.Server) (*bdev.MockBackend, initNvmeCmdFn) {
	engines := config.Server{}.Engines
	if sc != nil {
		engines = sc.Engines
	}
	mbb := bdev.NewMockBackend(&bmbc)
	scs := server.NewMockStorageControlService(log, engines, nil, nil,
		bdev.NewProvider(log, mbb), nil)

	return mbb, func(cmd *nvmeCmd) (*server.StorageControlService, *config.Server, error) {
		if cmd.isIOMMUEnabled == nil {
			cmd.setIOMMUChecker(func() (bool, error) {
				return true, nil
			})
		}
		scc := cmd.config
		if sc != nil {
			scc = sc
		}

		cmd.Logger.Tracef("mock storage control service set: %+v", scs)
		return scs, scc, nil
	}
}

func TestDaosServer_prepareNVMe(t *testing.T) {
	testHugeNodesStr := func(n int) string {
		return fmt.Sprintf("nodes_hp[0]=%d", n)
	}
	newPrepCmd := func() *prepareNVMeCmd {
		pdc := &prepareNVMeCmd{
			PCIBlockList: defaultMultiAddrList,
		}
		pdc.Args.PCIAllowList = defaultSingleAddrList
		return pdc
	}
	mockConfig := func() *config.Server {
		c := config.Server{}
		return &c
	}

	for name, tc := range map[string]struct {
		prepCmd       *prepareNVMeCmd
		cfg           *config.Server
		bmbc          bdev.MockBackendConfig
		iommuDisabled bool
		memInfo       *common.SysMemInfo
		expErr        error
		expPrepCalls  []storage.BdevPrepareRequest
	}{
		"hugepages disabled in config": {
			cfg:    new(config.Server).WithDisableHugepages(true),
			expErr: storage.FaultHugepagesDisabled,
		},
		"bad block pci address": {
			prepCmd: &prepareNVMeCmd{
				PCIBlockList: "invalid-pci-address",
			},
			expErr: errors.New("unexpected pci address"),
		},
		"bad allow pci address": {
			prepCmd: func() *prepareNVMeCmd {
				pc := prepareNVMeCmd{}
				pc.Args.PCIAllowList = "invalid-pci-address"
				return &pc
			}(),
			expErr: errors.New("unexpected pci address"),
		},
		"no meminfo specified": {
			expErr: errors.New("nil *common.SysMemInfo"),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
			},
		},
		"cli nr_hugepages unset; zero total global": {
			cfg:     new(config.Server).WithNrHugepages(1024),
			memInfo: &common.SysMemInfo{},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					// always set in local storage prepare to allow automatic detection
					EnableVMD: true,
					HugeNodes: testHugeNodesStr(config.ScanMinHugepageCount),
				},
			},
		},
		"cli nr_hugepages unset; zero total numa": {
			cfg: new(config.Server).WithNrHugepages(1024),
			memInfo: &common.SysMemInfo{
				NumaNodes: []common.MemInfo{
					{HugepagesTotal: 0},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					EnableVMD: true,
					HugeNodes: testHugeNodesStr(config.ScanMinHugepageCount),
				},
			},
		},
		"cli nr_hugepages set insufficient; zero total numa": {
			cfg: new(config.Server).WithNrHugepages(1024),
			prepCmd: &prepareNVMeCmd{
				NrHugepages: 64,
			},
			memInfo: &common.SysMemInfo{
				NumaNodes: []common.MemInfo{
					{HugepagesTotal: 0},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					EnableVMD: true,
					HugeNodes: testHugeNodesStr(config.ScanMinHugepageCount),
				},
			},
		},
		"cli nr_hugepages set; zero total numa": {
			cfg: new(config.Server).WithNrHugepages(1024),
			prepCmd: &prepareNVMeCmd{
				NrHugepages: 512,
			},
			memInfo: &common.SysMemInfo{
				NumaNodes: []common.MemInfo{
					{HugepagesTotal: 0},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					EnableVMD: true,
					HugeNodes: testHugeNodesStr(512),
				},
			},
		},
		"cli nr_hugepages unset; sufficient total global": {
			cfg: new(config.Server).WithNrHugepages(1024),
			memInfo: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					HugepagesTotal: 2048,
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					EnableVMD: true,
					// Value matching what already exists to avoid shrinking allocation.
					HugeNodes: testHugeNodesStr(2048),
				},
			},
		},
		"cli nr_hugepages unset; sufficient total numa": {
			cfg: new(config.Server).WithNrHugepages(1024),
			memInfo: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					HugepagesTotal: 2048,
				},
				NumaNodes: []common.MemInfo{
					{HugepagesTotal: 2046},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					EnableVMD: true,
					// Per-NUMA value used.
					HugeNodes: testHugeNodesStr(2046),
				},
			},
		},
		"cli nr_hugepages set; exceeds total numa": {
			cfg: new(config.Server).WithNrHugepages(1024),
			prepCmd: &prepareNVMeCmd{
				NrHugepages: 2050,
			},
			memInfo: &common.SysMemInfo{
				MemInfo: common.MemInfo{
					HugepagesTotal: 2048,
				},
				NumaNodes: []common.MemInfo{
					{HugepagesTotal: 2046},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					EnableVMD: true,
					// Requested value used.
					HugeNodes: testHugeNodesStr(2050),
				},
			},
		},
		"no devices; success": {
			memInfo: defSysMemInfo(),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					// always set in local storage prepare to allow automatic detection
					EnableVMD: true,
					HugeNodes: testHugeNodesStr(1024),
				},
			},
		},
		"succeeds; user params": {
			prepCmd: newPrepCmd(),
			memInfo: defSysMemInfo(),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList:       defaultSingleAddrList,
					PCIBlockList:       spaceSepMultiAddrList,
				},
				{
					HugeNodes:    testHugeNodesStr(defSysMemInfo().HugepagesTotal),
					PCIAllowList: defaultSingleAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					EnableVMD:    true,
				},
			},
		},
		"succeeds; different target user; multi allow list": {
			prepCmd: newPrepCmd().WithTargetUser("bob").WithPCIAllowList(defaultMultiAddrList),
			memInfo: defSysMemInfo(),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList:       spaceSepMultiAddrList,
					PCIBlockList:       spaceSepMultiAddrList,
				},
				{
					TargetUser:   "bob",
					HugeNodes:    testHugeNodesStr(defSysMemInfo().HugepagesTotal),
					PCIAllowList: spaceSepMultiAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					EnableVMD:    true,
				},
			},
		},
		"fails; user params": {
			prepCmd: newPrepCmd(),
			memInfo: defSysMemInfo(),
			bmbc: bdev.MockBackendConfig{
				PrepareErr: errors.New("backend prep setup failed"),
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList:       defaultSingleAddrList,
					PCIBlockList:       spaceSepMultiAddrList,
				},
				{
					TargetUser:   "bob",
					HugeNodes:    testHugeNodesStr(defSysMemInfo().HugepagesTotal),
					PCIAllowList: defaultSingleAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					EnableVMD:    true,
				},
			},
			expErr: errors.New("backend prep setup failed"),
		},
		"non-root; vfio disabled": {
			prepCmd: newPrepCmd().WithDisableVFIO(true),
			memInfo: defSysMemInfo(),
			expErr:  errors.New("VFIO can not be disabled"),
		},
		"non-root; iommu not detected": {
			iommuDisabled: true,
			memInfo:       defSysMemInfo(),
			expErr:        errors.New("no IOMMU capability detected"),
		},
		"root; vfio disabled; iommu not detected": {
			prepCmd:       newPrepCmd().WithTargetUser("root").WithDisableVFIO(true),
			memInfo:       defSysMemInfo(),
			iommuDisabled: true,
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList:       defaultSingleAddrList,
					PCIBlockList:       spaceSepMultiAddrList,
				},
				{
					HugeNodes:    testHugeNodesStr(defSysMemInfo().HugepagesTotal),
					TargetUser:   "root",
					PCIAllowList: defaultSingleAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					DisableVFIO:  true,
				},
			},
		},
		"config parameters ignored as cmd settings already exist": {
			prepCmd: newPrepCmd(),
			memInfo: defSysMemInfo(),
			cfg: new(config.Server).
				WithEngines(engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(8)))).
				WithBdevExclude(test.MockPCIAddr(9)).
				WithNrHugepages(2048),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList:       defaultSingleAddrList,
					PCIBlockList:       spaceSepMultiAddrList,
				},
				{
					HugeNodes:    testHugeNodesStr(defSysMemInfo().HugepagesTotal),
					PCIAllowList: defaultSingleAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					EnableVMD:    true,
				},
			},
		},
		"config parameters applied; disable vmd": {
			prepCmd: &prepareNVMeCmd{},
			memInfo: defSysMemInfo(),
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(7))),
				engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(8))),
			).
				WithBdevExclude(test.MockPCIAddr(9)).
				WithNrHugepages(1024).
				WithDisableVMD(true),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7),
						storage.BdevPciAddrSep, test.MockPCIAddr(8)),
					PCIBlockList: test.MockPCIAddr(9),
				},
				{
					HugeNodes: testHugeNodesStr(defSysMemInfo().HugepagesTotal),
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7),
						storage.BdevPciAddrSep, test.MockPCIAddr(8)),
					PCIBlockList: test.MockPCIAddr(9),
				},
			},
		},
		"config parameters applied; disable vfio": {
			prepCmd: newPrepCmd(),
			memInfo: defSysMemInfo(),
			cfg:     new(config.Server).WithDisableVFIO(true),
			expErr:  errors.New("can not be disabled if running as non-root"),
		},
		"nil config; parameters not applied (simulates effect of --ignore-config)": {
			prepCmd: &prepareNVMeCmd{},
			memInfo: defSysMemInfo(),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					HugeNodes: testHugeNodesStr(defSysMemInfo().HugepagesTotal),
					EnableVMD: true,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			if tc.cfg == nil {
				tc.cfg = mockConfig()
			}

			mbb, mockInitFn := getMockNvmeCmdInit(log, tc.bmbc, tc.cfg)

			if tc.prepCmd == nil {
				tc.prepCmd = &prepareNVMeCmd{}
			}
			tc.prepCmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			tc.prepCmd.config = tc.cfg
			tc.prepCmd.setIOMMUChecker(func() (bool, error) {
				return !tc.iommuDisabled, nil
			})
			if err := tc.prepCmd.initWith(mockInitFn); err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			req := storage.BdevPrepareRequest{
				HugepageCount: tc.prepCmd.NrHugepages,
				TargetUser:    tc.prepCmd.TargetUser,
				PCIAllowList:  tc.prepCmd.Args.PCIAllowList,
				PCIBlockList:  tc.prepCmd.PCIBlockList,
				DisableVFIO:   tc.prepCmd.DisableVFIO,
			}

			gotErr := prepareNVMe(req, &tc.prepCmd.nvmeCmd, tc.memInfo)
			test.CmpErr(t, tc.expErr, gotErr)

			// If empty TargetUser in cmd, expect current user in call.
			if tc.prepCmd.TargetUser == "" {
				for i := range tc.expPrepCalls {
					if tc.expPrepCalls[i].CleanSpdkHugepages {
						continue
					}
					tc.expPrepCalls[i].TargetUser = getCurrentUsername(t)
				}
			}

			mbb.RLock()
			if diff := cmp.Diff(tc.expPrepCalls, mbb.PrepareCalls, defPrepCmpOpt); diff != "" {
				t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
			}
			if len(mbb.ResetCalls) != 0 {
				t.Fatalf("unexpected number of reset calls, want 0 got %d",
					len(mbb.ResetCalls))
			}
			mbb.RUnlock()
		})
	}
}

func TestDaosServer_resetNVMe(t *testing.T) {
	// bdev mock commands
	newResetCmd := func() *resetNVMeCmd {
		rdc := &resetNVMeCmd{
			PCIBlockList: defaultMultiAddrList,
		}
		rdc.Args.PCIAllowList = defaultSingleAddrList
		return rdc
	}

	defCleanCall := storage.BdevPrepareRequest{
		CleanSpdkHugepages: true,
		CleanSpdkLockfiles: true,
		PCIAllowList:       defaultSingleAddrList,
		PCIBlockList:       spaceSepMultiAddrList,
	}

	for name, tc := range map[string]struct {
		resetCmd      *resetNVMeCmd
		cfg           *config.Server
		bmbc          bdev.MockBackendConfig
		iommuDisabled bool
		expErr        error
		expCleanCall  *storage.BdevPrepareRequest
		expResetCalls []storage.BdevPrepareRequest
	}{
		"hugepages disabled in config": {
			cfg:    new(config.Server).WithDisableHugepages(true),
			expErr: storage.FaultHugepagesDisabled,
		},
		"no devices; success": {
			expCleanCall: &storage.BdevPrepareRequest{
				CleanSpdkHugepages:    true,
				CleanSpdkLockfiles:    true,
				CleanSpdkLockfilesAny: true,
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					EnableVMD: true,
					// If empty TargetUser in cmd, expect current user in call.
					TargetUser: getCurrentUsername(t),
					Reset_:     true,
				},
			},
		},
		"succeeds; user params; vmd prepared": {
			resetCmd: newResetCmd(),
			bmbc: bdev.MockBackendConfig{
				ResetRes: &storage.BdevPrepareResponse{
					// Response flag indicates VMD is active and triggers
					// second reset call.
					VMDPrepared: true,
				},
			},
			expCleanCall: &defCleanCall,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					PCIAllowList: defaultSingleAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					EnableVMD:    true,
					TargetUser:   getCurrentUsername(t),
					Reset_:       true,
				},
				// VMD was acted on in first call so reset called a second time
				// without allow list. EnableVMD is false to prevent VMD domain
				// addresses being automatically added to allow list in backend.
				{
					TargetUser: getCurrentUsername(t),
					Reset_:     true,
				},
			},
		},
		"succeeds; different target user; multi allow list; vmd not prepared": {
			resetCmd: newResetCmd().
				WithTargetUser("bob").
				WithPCIAllowList(defaultMultiAddrList),
			expCleanCall: &storage.BdevPrepareRequest{
				CleanSpdkHugepages: true,
				CleanSpdkLockfiles: true,
				PCIAllowList:       spaceSepMultiAddrList,
				PCIBlockList:       spaceSepMultiAddrList,
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					EnableVMD:    true,
					TargetUser:   "bob",
					PCIAllowList: spaceSepMultiAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					Reset_:       true,
				},
			},
		},
		"fails; user params": {
			resetCmd: newResetCmd(),
			bmbc: bdev.MockBackendConfig{
				ResetRes: &storage.BdevPrepareResponse{
					VMDPrepared: true,
				},
				ResetErr: errors.New("backend prep reset failed"),
			},
			expCleanCall: &defCleanCall,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					EnableVMD:    true,
					PCIAllowList: defaultSingleAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					TargetUser:   getCurrentUsername(t),
					Reset_:       true,
				},
			},
			expErr: errors.New("backend prep reset failed"),
		},
		"non-root; vfio disabled": {
			resetCmd: newResetCmd().WithDisableVFIO(true),
			expErr:   errors.New("VFIO can not be disabled"),
		},
		"non-root; iommu not detected": {
			iommuDisabled: true,
			expErr:        errors.New("no IOMMU capability detected"),
		},
		"root; vfio disabled; iommu not detected": {
			resetCmd:      newResetCmd().WithTargetUser("root").WithDisableVFIO(true),
			iommuDisabled: true,
			expCleanCall:  &defCleanCall,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					TargetUser:   "root",
					PCIAllowList: defaultSingleAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					DisableVFIO:  true,
					Reset_:       true,
				},
			},
		},
		"config parameters ignored; settings exist": {
			resetCmd: newResetCmd(),
			cfg: new(config.Server).
				WithEngines(engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(8)))).
				WithBdevExclude(test.MockPCIAddr(9)).
				WithNrHugepages(1024),
			expCleanCall: &defCleanCall,
			expResetCalls: []storage.BdevPrepareRequest{
				{
					EnableVMD:    true,
					PCIAllowList: defaultSingleAddrList,
					PCIBlockList: spaceSepMultiAddrList,
					TargetUser:   getCurrentUsername(t),
					Reset_:       true,
				},
			},
		},
		"config parameters applied; disable vmd": {
			resetCmd: &resetNVMeCmd{},
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(7))),
				engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(8))),
			).
				WithBdevExclude(test.MockPCIAddr(9)).
				WithNrHugepages(1024).
				WithDisableVMD(true),
			expCleanCall: &storage.BdevPrepareRequest{
				CleanSpdkHugepages: true,
				CleanSpdkLockfiles: true,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7),
					storage.BdevPciAddrSep, test.MockPCIAddr(8)),
				PCIBlockList: test.MockPCIAddr(9),
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7),
						storage.BdevPciAddrSep, test.MockPCIAddr(8)),
					PCIBlockList: test.MockPCIAddr(9),
					TargetUser:   getCurrentUsername(t),
					Reset_:       true,
				},
			},
		},
		"config parameters applied; disable vfio": {
			resetCmd: newResetCmd(),
			cfg:      new(config.Server).WithDisableVFIO(true),
			expErr:   errors.New("can not be disabled if running as non-root"),
		},
		"nil config; parameters not applied (simulates effect of --ignore-config)": {
			resetCmd: &resetNVMeCmd{},
			expCleanCall: &storage.BdevPrepareRequest{
				CleanSpdkHugepages:    true,
				CleanSpdkLockfiles:    true,
				CleanSpdkLockfilesAny: true,
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					TargetUser: getCurrentUsername(t),
					Reset_:     true,
					EnableVMD:  true,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb, mockInitFn := getMockNvmeCmdInit(log, tc.bmbc, tc.cfg)

			if tc.resetCmd == nil {
				tc.resetCmd = &resetNVMeCmd{}
			}
			tc.resetCmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			tc.resetCmd.config = tc.cfg
			tc.resetCmd.setIOMMUChecker(func() (bool, error) {
				return !tc.iommuDisabled, nil
			})
			if err := tc.resetCmd.initWith(mockInitFn); err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			req := storage.BdevPrepareRequest{
				TargetUser:   tc.resetCmd.TargetUser,
				PCIAllowList: tc.resetCmd.Args.PCIAllowList,
				PCIBlockList: tc.resetCmd.PCIBlockList,
				DisableVFIO:  tc.resetCmd.DisableVFIO,
				Reset_:       true,
			}

			gotErr := resetNVMe(req, &tc.resetCmd.nvmeCmd)
			test.CmpErr(t, tc.expErr, gotErr)

			mbb.RLock()
			log.Tracef("mock bdev backend prepare calls: %+v", mbb.PrepareCalls)
			log.Tracef("mock bdev backend reset calls: %+v", mbb.ResetCalls)

			// Call to clean hugepages should always be expected first.
			switch len(mbb.PrepareCalls) {
			case 0:
				if tc.expCleanCall != nil {
					t.Fatalf("unexpected number of prepare calls, want 1 got 0")
				}
			case 1:
				if diff := cmp.Diff(tc.expCleanCall, &mbb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected clean hugepage calls (-want, +got):\n%s\n", diff)
				}
			default:
				if tc.expCleanCall != nil {
					t.Fatalf("unexpected number of prepare calls, want 1 got %d",
						len(mbb.PrepareCalls))
				}
			}

			if diff := cmp.Diff(tc.expResetCalls, mbb.ResetCalls); diff != "" {
				t.Fatalf("unexpected reset calls (-want, +got):\n%s\n", diff)
			}
			mbb.RUnlock()
		})
	}
}

func TestDaosServer_getVMDState(t *testing.T) {
	fa := false
	tr := true

	for name, tc := range map[string]struct {
		cfg           *config.Server
		cmdDisableVMD bool
		expOut        bool
	}{
		"nil cmd cfg": {
			expOut: true,
		},
		"vmd state not specified in cfg": {
			cfg:    &config.Server{},
			expOut: true,
		},
		"vmd not disabled in cfg": {
			cfg: &config.Server{
				DisableVMD: &fa,
			},
			expOut: true,
		},
		"vmd disabled in cfg": {
			cfg: &config.Server{
				DisableVMD: &tr,
			},
		},
		"vmd disabled on commandline": {
			cfg:           &config.Server{},
			cmdDisableVMD: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			scanCmd := &scanNVMeCmd{}
			scanCmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			scanCmd.config = tc.cfg
			scanCmd.DisableVMD = tc.cmdDisableVMD

			test.AssertEqual(t, tc.expOut, scanCmd.getVMDState(), "unexpected VMD state")
		})
	}
}

func TestDaosServer_scanNVMe(t *testing.T) {
	for name, tc := range map[string]struct {
		scanCmd       *scanNVMeCmd
		cfg           *config.Server
		iommuDisabled bool
		skipPrep      bool
		memInfo       *common.SysMemInfo
		expPrepCalls  []storage.BdevPrepareRequest
		expResetCalls []storage.BdevPrepareRequest
		bmbc          bdev.MockBackendConfig
		expErr        error
		expScanCall   *storage.BdevScanRequest
		expScanResp   *storage.BdevScanResponse
	}{
		"hugepages disabled in config": {
			cfg:    new(config.Server).WithDisableHugepages(true),
			expErr: storage.FaultHugepagesDisabled,
		},
		"normal scan": {
			bmbc: bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					TargetUser: getCurrentUsername(t),
					HugeNodes:  defHugeNodesStr,
					EnableVMD:  true,
				},
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					TargetUser: getCurrentUsername(t),
					EnableVMD:  true,
					Reset_:     true,
				},
			},
			expScanCall: &storage.BdevScanRequest{},
		},
		"failed scan": {
			bmbc: bdev.MockBackendConfig{
				ScanErr: errors.New("fail"),
			},
			expErr: errors.New("fail"),
		},
		"devices filtered by config": {
			bmbc: bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
						storage.MockNvmeController(2),
						storage.MockNvmeController(3),
					},
				},
			},
			cfg: (&config.Server{}).WithEngines(
				(&engine.Config{}).WithStorage(storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(test.MockPCIAddr(1))),
				(&engine.Config{}).WithStorage(storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(test.MockPCIAddr(2))),
			),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList:       spaceSepMultiAddrList,
				},
				{
					TargetUser:   getCurrentUsername(t),
					EnableVMD:    true,
					PCIAllowList: spaceSepMultiAddrList,
					HugeNodes:    defHugeNodesStr,
				},
				{
					CleanSpdkHugepages: true,
					CleanSpdkLockfiles: true,
					PCIAllowList:       spaceSepMultiAddrList,
				},
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					TargetUser:   getCurrentUsername(t),
					EnableVMD:    true,
					PCIAllowList: spaceSepMultiAddrList,
					Reset_:       true,
				},
			},
			expScanCall: &storage.BdevScanRequest{
				DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddr(1),
					test.MockPCIAddr(2)),
			},
		},
		"no devices specified in config; vmd disabled in config": {
			bmbc: bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
						storage.MockNvmeController(2),
						storage.MockNvmeController(3),
					},
				},
			},
			cfg: (&config.Server{}).WithEngines(
				(&engine.Config{}).WithStorage(),
			).WithDisableVMD(true),
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					TargetUser: getCurrentUsername(t),
					HugeNodes:  defHugeNodesStr,
				},
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					TargetUser: getCurrentUsername(t),
					Reset_:     true,
				},
			},
			expScanCall: &storage.BdevScanRequest{},
		},
		"nil config; parameters not applied (simulates effect of --ignore-config)": {
			bmbc: bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
						storage.MockNvmeController(2),
						storage.MockNvmeController(3),
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
				{
					TargetUser: getCurrentUsername(t),
					HugeNodes:  defHugeNodesStr,
					EnableVMD:  true,
				},
				{
					CleanSpdkHugepages:    true,
					CleanSpdkLockfiles:    true,
					CleanSpdkLockfilesAny: true,
				},
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{
					TargetUser: getCurrentUsername(t),
					EnableVMD:  true,
					Reset_:     true,
				},
			},
			expScanCall: &storage.BdevScanRequest{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb, mockInitFn := getMockNvmeCmdInit(log, tc.bmbc, tc.cfg)

			if tc.expScanResp == nil {
				tc.expScanResp = tc.bmbc.ScanRes
			}
			if tc.scanCmd == nil {
				tc.scanCmd = &scanNVMeCmd{}
			}
			tc.scanCmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			tc.scanCmd.config = tc.cfg
			tc.scanCmd.setIOMMUChecker(func() (bool, error) {
				return !tc.iommuDisabled, nil
			})
			tc.scanCmd.SkipPrep = tc.skipPrep
			if err := tc.scanCmd.initWith(mockInitFn); err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			if tc.memInfo == nil {
				tc.memInfo = defSysMemInfo()
			}

			gotResp, gotErr := scanNVMe(tc.scanCmd, tc.memInfo)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expScanResp, gotResp, defPrepCmpOpt); diff != "" {
				t.Fatalf("unexpected scan resp (-want, +got):\n%s\n", diff)
			}

			mbb.RLock()
			if len(mbb.ScanCalls) != 1 {
				t.Fatalf("unexpected number of scan calls, want 1 got %d", len(mbb.ScanCalls))
			}
			if diff := cmp.Diff(tc.expScanCall, &mbb.ScanCalls[0], defPrepCmpOpt); diff != "" {
				t.Fatalf("unexpected scan calls (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expPrepCalls, mbb.PrepareCalls, defPrepCmpOpt); diff != "" {
				t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResetCalls, mbb.ResetCalls, defPrepCmpOpt); diff != "" {
				t.Fatalf("unexpected reset calls (-want, +got):\n%s\n", diff)
			}
			mbb.RUnlock()
		})
	}
}

func TestDaosServer_NVMe_Commands(t *testing.T) {
	multPCIAddrsSpaceSep := "0000:80:00.0 0000:81:00.0"
	multPCIAddrsCommaSep := "0000:80:00.0,0000:81:00.0"

	runCmdTests(t, []cmdTest{
		{
			"Prepare drives with all opts; space separated PCI addresses",
			fmt.Sprintf("nvme prepare --pci-block-list %s --hugepages 8192 --target-user bob --disable-vfio "+
				"-l /tmp/foo "+multPCIAddrsSpaceSep, multPCIAddrsSpaceSep),
			"",
			errors.New("unexpected commandline arguments"),
		},
		{
			"Prepare drives with all opts; comma separated PCI addresses",
			fmt.Sprintf("nvme prepare --pci-block-list %s --hugepages 8192 --target-user bob --disable-vfio "+
				"-l /tmp/foo "+multPCIAddrsCommaSep, multPCIAddrsCommaSep),
			printCommand(t, (&prepareNVMeCmd{
				PCIBlockList: multPCIAddrsCommaSep,
				NrHugepages:  8192,
				TargetUser:   "bob",
				DisableVFIO:  true,
			}).WithPCIAllowList(multPCIAddrsCommaSep)),
			nil,
		},
		{
			"Prepare drives; bad opt",
			fmt.Sprintf("nvme prepare --pcx-block-list %s --hugepages 8192 --target-user bob --disable-vfio "+
				"-l /tmp/foo "+multPCIAddrsCommaSep, multPCIAddrsCommaSep),
			"",
			errors.New("unknown"),
		},
		{
			"Reset drives with all opts",
			fmt.Sprintf("nvme reset --pci-block-list %s --target-user bob --disable-vfio -l /tmp/foo "+
				multPCIAddrsCommaSep, multPCIAddrsCommaSep),
			printCommand(t, (&resetNVMeCmd{
				PCIBlockList: multPCIAddrsCommaSep,
				TargetUser:   "bob",
				DisableVFIO:  true,
			}).WithPCIAllowList(multPCIAddrsCommaSep)),
			nil,
		},
		{
			"Reset drives; bad opt",
			fmt.Sprintf("nvme reset --pci-block-list %s --target-user bob --disble-vfio -l /tmp/foo "+
				multPCIAddrsCommaSep, multPCIAddrsCommaSep),
			"",
			errors.New("unknown"),
		},
		{
			"Scan drives",
			"nvme scan",
			printCommand(t, &scanNVMeCmd{}),
			nil,
		},
		{
			"Scan drives; disable vmd",
			"nvme scan --disable-vmd",
			printCommand(t, &scanNVMeCmd{DisableVMD: true}),
			nil,
		},
		{
			"Scan drives; skip prep",
			"nvme scan --skip-prep",
			printCommand(t, &scanNVMeCmd{SkipPrep: true}),
			nil,
		},
	})
}

func genSetNVMeHelpers(log logging.Logger, bmbc bdev.MockBackendConfig) func(*mainOpts) {
	_, mockInit := getMockNvmeCmdInit(log, bmbc, nil)
	return func(opts *mainOpts) {
		opts.nvmeInitHelper = mockInit
	}
}

// TestDaosServer_NVMe_Commands_JSON verifies that when the JSON-output flag is set only JSON is
// printed to standard out. Test cases should cover all scm subcommand variations.
func TestDaosServer_NVMe_Commands_JSON(t *testing.T) {
	// Use a normal logger to verify that we don't mess up JSON output.
	log, buf := logging.NewTestCommandLineLogger()

	c1 := storage.MockNvmeController(1)

	runJSONCmdTests(t, log, buf, []jsonCmdTest{
		{
			"Prepare SSDs",
			"nvme prepare -j",
			genSetNVMeHelpers(log, bdev.MockBackendConfig{
				PrepareRes: &storage.BdevPrepareResponse{},
			}),
			nil,
			nil,
		},
		{
			"Prepare SSDs; returns error",
			"nvme prepare -j",
			genSetNVMeHelpers(log, bdev.MockBackendConfig{
				PrepareErr: errors.New("bad prep"),
			}),
			nil,
			errors.New("nvme prepare backend: bad prep"),
		},
		{
			"Reset SSDs",
			"nvme reset -j",
			genSetNVMeHelpers(log, bdev.MockBackendConfig{
				PrepareRes: &storage.BdevPrepareResponse{},
			}),
			nil,
			nil,
		},
		{
			"Reset SSDs; returns error",
			"nvme reset -j",
			genSetNVMeHelpers(log, bdev.MockBackendConfig{
				ResetErr: errors.New("bad reset"),
			}),
			nil,
			errors.New("nvme reset backend: bad reset"),
		},
		{
			"Scan SSDs",
			"nvme scan -j",
			genSetNVMeHelpers(log, bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{c1},
				},
			}),
			storage.NvmeControllers{c1},
			nil,
		},
		{
			"Scan SSDs; returns error",
			"nvme scan -j",
			genSetNVMeHelpers(log, bdev.MockBackendConfig{
				ScanErr: errors.New("bad scan"),
			}),
			nil,
			errors.New("nvme scan backend: bad scan"),
		},
		{
			"Scan SSDs; config missing",
			fmt.Sprintf("nvme scan -j -o %s", badDir),
			genSetNVMeHelpers(log, bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{c1},
				},
			}),
			nil,
			errors.New(fmt.Sprintf("failed to load config from %s: stat %s: "+
				"no such file or directory", badDir, badDir)),
		},
	})
}

func checkCfgIgnored(t *testing.T, bsc baseScanCmd) {
	t.Helper()
	cm := bsc.optCfgCmd.cfgCmd
	if !cm.IgnoreConfig {
		t.Fatal("expected ignore config flag to be true")
	}
	if cm.config != nil {
		t.Fatal("expected config in cmd to be nil")
	}
}

func checkCfgNotIgnored(t *testing.T, bsc baseScanCmd) {
	t.Helper()
	cm := bsc.optCfgCmd.cfgCmd
	if cm.IgnoreConfig {
		t.Fatal("expected ignore config flag to be false")
	}
	if cm.config == nil {
		t.Fatal("expected config in cmd not to be nil")
	}
}

// Verify that when --ignore-config is supplied on commandline, cmd.config is nil.
func TestDaosServer_NVMe_Commands_Config(t *testing.T) {
	for name, tc := range map[string]struct {
		cmd       string
		bmbc      bdev.MockBackendConfig
		optsCheck func(t *testing.T, o *mainOpts)
		expErr    error
	}{
		"prepare; ignore config": {
			cmd: "nvme prepare --ignore-config",
			optsCheck: func(t *testing.T, o *mainOpts) {
				checkCfgIgnored(t, o.NVMe.Prepare.nvmeCmd.baseScanCmd)
			},
		},
		"prepare; read config": {
			cmd: "nvme prepare",
			optsCheck: func(t *testing.T, o *mainOpts) {
				checkCfgNotIgnored(t, o.NVMe.Prepare.nvmeCmd.baseScanCmd)
			},
		},
		"reset; ignore config": {
			cmd: "nvme reset --ignore-config",
			optsCheck: func(t *testing.T, o *mainOpts) {
				checkCfgIgnored(t, o.NVMe.Reset.nvmeCmd.baseScanCmd)
			},
		},
		"reset; read config": {
			cmd: "nvme reset",
			optsCheck: func(t *testing.T, o *mainOpts) {
				checkCfgNotIgnored(t, o.NVMe.Reset.nvmeCmd.baseScanCmd)
			},
		},
		"scan; ignore config": {
			cmd: "nvme scan --ignore-config",
			optsCheck: func(t *testing.T, o *mainOpts) {
				checkCfgIgnored(t, o.NVMe.Scan.nvmeCmd.baseScanCmd)
			},
		},
		"scan; read config": {
			cmd: "nvme scan",
			optsCheck: func(t *testing.T, o *mainOpts) {
				checkCfgNotIgnored(t, o.NVMe.Scan.nvmeCmd.baseScanCmd)
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var opts mainOpts
			genSetNVMeHelpers(log, tc.bmbc)(&opts)
			_ = parseOpts(strings.Split(tc.cmd, " "), &opts, log)

			log.Infof("opts: %+v", opts.NVMe)
			tc.optsCheck(t, &opts)
		})
	}
}
