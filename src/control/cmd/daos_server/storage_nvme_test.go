//
// (C) Copyright 2022-2024 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
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
	// bdev req parameters
	testNrHugepages := 42
	// bdev mock commands
	newPrepCmd := func() *prepareNVMeCmd {
		pdc := &prepareNVMeCmd{
			NrHugepages:  testNrHugepages,
			PCIBlockList: defaultMultiAddrList,
		}
		pdc.Args.PCIAllowList = defaultSingleAddrList
		return pdc
	}

	for name, tc := range map[string]struct {
		prepCmd       *prepareNVMeCmd
		cfg           *config.Server
		bmbc          bdev.MockBackendConfig
		iommuDisabled bool
		expErr        error
		expPrepCall   *storage.BdevPrepareRequest
	}{
		"no devices; success": {
			expPrepCall: &storage.BdevPrepareRequest{
				// always set in local storage prepare to allow automatic detection
				EnableVMD: true,
			},
		},
		"succeeds; user params": {
			prepCmd: newPrepCmd(),
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: testNrHugepages,
				PCIAllowList:  defaultSingleAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				EnableVMD:     true,
			},
		},
		"succeeds; different target user; multi allow list": {
			prepCmd: newPrepCmd().WithTargetUser("bob").WithPCIAllowList(defaultMultiAddrList),
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser:    "bob",
				HugepageCount: testNrHugepages,
				PCIAllowList:  spaceSepMultiAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				EnableVMD:     true,
			},
		},
		"fails; user params": {
			prepCmd: newPrepCmd(),
			bmbc: bdev.MockBackendConfig{
				PrepareErr: errors.New("backend prep setup failed"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: testNrHugepages,
				PCIAllowList:  defaultSingleAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				EnableVMD:     true,
			},
			expErr: errors.New("backend prep setup failed"),
		},
		"non-root; vfio disabled": {
			prepCmd: newPrepCmd().WithDisableVFIO(true),
			expErr:  errors.New("VFIO can not be disabled"),
		},
		"non-root; iommu not detected": {
			iommuDisabled: true,
			expErr:        errors.New("no IOMMU capability detected"),
		},
		"root; vfio disabled; iommu not detected": {
			prepCmd:       newPrepCmd().WithTargetUser("root").WithDisableVFIO(true),
			iommuDisabled: true,
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: testNrHugepages,
				TargetUser:    "root",
				PCIAllowList:  defaultSingleAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				DisableVFIO:   true,
			},
		},
		"config parameters ignored as cmd settings already exist": {
			prepCmd: newPrepCmd(),
			cfg: new(config.Server).
				WithEngines(engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(8)))).
				WithBdevExclude(test.MockPCIAddr(9)).
				WithNrHugepages(1024),
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: testNrHugepages,
				PCIAllowList:  defaultSingleAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				EnableVMD:     true,
			},
		},
		"config parameters applied; disable vmd": {
			prepCmd: &prepareNVMeCmd{},
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
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 1024,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7), storage.BdevPciAddrSep,
					test.MockPCIAddr(8)),
				PCIBlockList: test.MockPCIAddr(9),
			},
		},
		"config parameters applied; disable vfio": {
			prepCmd: newPrepCmd(),
			cfg:     new(config.Server).WithDisableVFIO(true),
			expErr:  errors.New("can not be disabled if running as non-root"),
		},
		"config parameters applied; legacy storage": {
			prepCmd: &prepareNVMeCmd{},
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().
					WithLegacyStorage(engine.LegacyStorage{
						BdevClass: storage.ClassNvme,
						BdevConfig: storage.BdevConfig{
							DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddr(7)),
						},
					}),
				engine.NewConfig().
					WithLegacyStorage(engine.LegacyStorage{
						BdevClass: storage.ClassNvme,
						BdevConfig: storage.BdevConfig{
							DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddr(8)),
						},
					}),
			).
				WithBdevExclude(test.MockPCIAddr(9)).
				WithNrHugepages(1024).
				WithDisableVMD(true),
			expPrepCall: &storage.BdevPrepareRequest{
				HugepageCount: 1024,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7), storage.BdevPciAddrSep,
					test.MockPCIAddr(8)),
				PCIBlockList: test.MockPCIAddr(9),
			},
		},
		"nil config; parameters not applied (simulates effect of --ignore-config)": {
			prepCmd: &prepareNVMeCmd{},
			expPrepCall: &storage.BdevPrepareRequest{
				EnableVMD: true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb, mockInitFn := getMockNvmeCmdInit(log, tc.bmbc, tc.cfg)

			nvmeCmdInit = mockInitFn
			defer func() {
				nvmeCmdInit = initNvmeCmd
			}()

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

			req := storage.BdevPrepareRequest{
				HugepageCount: tc.prepCmd.NrHugepages,
				TargetUser:    tc.prepCmd.TargetUser,
				PCIAllowList:  tc.prepCmd.Args.PCIAllowList,
				PCIBlockList:  tc.prepCmd.PCIBlockList,
				DisableVFIO:   tc.prepCmd.DisableVFIO,
			}

			gotErr := prepareNVMe(req, &tc.prepCmd.nvmeCmd)
			test.CmpErr(t, tc.expErr, gotErr)

			mbb.RLock()
			if tc.expPrepCall == nil {
				if len(mbb.PrepareCalls) != 0 {
					t.Fatalf("unexpected number of prepare calls, want 0 got %d",
						len(mbb.PrepareCalls))
				}
			} else {
				if len(mbb.PrepareCalls) != 1 {
					t.Fatalf("unexpected number of prepare calls, want 1 got %d",
						len(mbb.PrepareCalls))
				}
				// If empty TargetUser in cmd, expect current user in call.
				if tc.prepCmd.TargetUser == "" {
					tc.expPrepCall.TargetUser = getCurrentUsername(t)
				}
				if diff := cmp.Diff(*tc.expPrepCall, mbb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
				}
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

	for name, tc := range map[string]struct {
		resetCmd      *resetNVMeCmd
		cfg           *config.Server
		bmbc          bdev.MockBackendConfig
		iommuDisabled bool
		expErr        error
		expResetCalls []storage.BdevPrepareRequest
	}{
		"no devices; success": {
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
		"config parameters applied; legacy storage": {
			resetCmd: &resetNVMeCmd{},
			cfg: new(config.Server).WithEngines(
				engine.NewConfig().
					WithLegacyStorage(engine.LegacyStorage{
						BdevClass: storage.ClassNvme,
						BdevConfig: storage.BdevConfig{
							DeviceList: storage.MustNewBdevDeviceList(
								test.MockPCIAddr(7)),
						},
					}),
				engine.NewConfig().
					WithLegacyStorage(engine.LegacyStorage{
						BdevClass: storage.ClassNvme,
						BdevConfig: storage.BdevConfig{
							DeviceList: storage.MustNewBdevDeviceList(
								test.MockPCIAddr(8)),
						},
					}),
			).
				WithBdevExclude(test.MockPCIAddr(9)).
				WithNrHugepages(1024).
				WithDisableVMD(true),
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
		"nil config; parameters not applied (simulates effect of --ignore-config)": {
			resetCmd: &resetNVMeCmd{},
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

			nvmeCmdInit = mockInitFn
			defer func() {
				nvmeCmdInit = initNvmeCmd
			}()

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
			// Call to clean hugepages should always be expected first.
			if len(mbb.PrepareCalls) != 1 {
				t.Fatalf("unexpected number of prepare calls, want 1w got %d",
					len(mbb.PrepareCalls))
			}
			if diff := cmp.Diff(storage.BdevPrepareRequest{CleanHugepagesOnly: true},
				mbb.PrepareCalls[0]); diff != "" {
				t.Fatalf("unexpected clean hugepage calls (-want, +got):\n%s\n", diff)
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
	cmpopt := cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
		if x == nil && y == nil {
			return true
		}
		return x.Equals(y)
	})

	for name, tc := range map[string]struct {
		scanCmd       *scanNVMeCmd
		cfg           *config.Server
		iommuDisabled bool
		skipPrep      bool
		expPrepCalls  []storage.BdevPrepareRequest
		expResetCalls []storage.BdevPrepareRequest
		bmbc          bdev.MockBackendConfig
		expErr        error
		expScanCall   *storage.BdevScanRequest
		expScanResp   *storage.BdevScanResponse
	}{
		"normal scan": {
			bmbc: bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
					},
				},
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				{TargetUser: getCurrentUsername(t), EnableVMD: true},
				{CleanHugepagesOnly: true},
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{TargetUser: getCurrentUsername(t), EnableVMD: true, Reset_: true},
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
					TargetUser:   getCurrentUsername(t),
					EnableVMD:    true,
					PCIAllowList: spaceSepMultiAddrList,
				},
				{CleanHugepagesOnly: true},
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
				{TargetUser: getCurrentUsername(t)},
				{CleanHugepagesOnly: true},
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{TargetUser: getCurrentUsername(t), Reset_: true},
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
				{TargetUser: getCurrentUsername(t), EnableVMD: true},
				{CleanHugepagesOnly: true},
			},
			expResetCalls: []storage.BdevPrepareRequest{
				{TargetUser: getCurrentUsername(t), EnableVMD: true, Reset_: true},
			},
			expScanCall: &storage.BdevScanRequest{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb, mockInitFn := getMockNvmeCmdInit(log, tc.bmbc, tc.cfg)

			nvmeCmdInit = mockInitFn
			defer func() {
				nvmeCmdInit = initNvmeCmd
			}()

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

			gotResp, gotErr := scanNVMe(tc.scanCmd)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expScanResp, gotResp, cmpopt); diff != "" {
				t.Fatalf("unexpected scan resp (-want, +got):\n%s\n", diff)
			}

			mbb.RLock()
			if len(mbb.ScanCalls) != 1 {
				t.Fatalf("unexpected number of scan calls, want 1 got %d", len(mbb.ScanCalls))
			}
			if diff := cmp.Diff(tc.expScanCall, &mbb.ScanCalls[0], cmpopt); diff != "" {
				t.Fatalf("unexpected scan calls (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expPrepCalls, mbb.PrepareCalls, cmpopt); diff != "" {
				t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResetCalls, mbb.ResetCalls, cmpopt); diff != "" {
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

// TestDaosServer_NVMe_Commands_JSON verifies that when the JSON-output flag is set only JSON is
// printed to standard out. Test cases should cover all scm subcommand variations.
func TestDaosServer_NVMe_Commands_JSON(t *testing.T) {
	// Use a normal logger to verify that we don't mess up JSON output.
	log, buf := logging.NewTestCommandLineLogger()

	genApplyMockFn := func(bmbc bdev.MockBackendConfig, sc *config.Server) func(*testing.T) {
		_, mockInitFn := getMockNvmeCmdInit(log, bmbc, sc)

		return func(_ *testing.T) {
			nvmeCmdInit = mockInitFn
		}
	}

	genCleanupMockFn := func() func(*testing.T) {
		return func(_ *testing.T) {
			nvmeCmdInit = initNvmeCmd
		}
	}

	c1 := storage.MockNvmeController(1)

	runJSONCmdTests(t, log, buf, []jsonCmdTest{
		{
			"Prepare SSDs",
			"nvme prepare -j",
			genApplyMockFn(bdev.MockBackendConfig{
				PrepareRes: &storage.BdevPrepareResponse{},
			}, nil),
			genCleanupMockFn(),
			nil,
			nil,
		},
		{
			"Prepare SSDs; returns error",
			"nvme prepare -j",
			genApplyMockFn(bdev.MockBackendConfig{
				PrepareErr: errors.New("bad prep"),
			}, nil),
			genCleanupMockFn(),
			nil,
			errors.New("nvme prepare backend: bad prep"),
		},
		{
			"Reset SSDs",
			"nvme reset -j",
			genApplyMockFn(bdev.MockBackendConfig{
				PrepareRes: &storage.BdevPrepareResponse{},
			}, nil),
			genCleanupMockFn(),
			nil,
			nil,
		},
		{
			"Reset SSDs; returns error",
			"nvme reset -j",
			genApplyMockFn(bdev.MockBackendConfig{
				ResetErr: errors.New("bad reset"),
			}, nil),
			genCleanupMockFn(),
			nil,
			errors.New("nvme reset backend: bad reset"),
		},
		{
			"Scan SSDs",
			"nvme scan -j",
			genApplyMockFn(bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{c1},
				},
			}, nil),
			genCleanupMockFn(),
			storage.NvmeControllers{c1},
			nil,
		},
		{
			"Scan SSDs; returns error",
			"nvme scan -j",
			genApplyMockFn(bdev.MockBackendConfig{
				ScanErr: errors.New("bad scan"),
			}, nil),
			genCleanupMockFn(),
			nil,
			errors.New("nvme scan backend: bad scan"),
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

			_, mockInitFn := getMockNvmeCmdInit(log, tc.bmbc, nil)

			nvmeCmdInit = mockInitFn
			defer func() {
				nvmeCmdInit = initNvmeCmd
			}()

			var opts mainOpts
			_ = parseOpts(strings.Split(tc.cmd, " "), &opts, log)

			log.Infof("opts: %+v", opts.NVMe)
			tc.optsCheck(t, &opts)
		})
	}
}
