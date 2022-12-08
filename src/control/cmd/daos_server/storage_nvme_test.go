//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os/user"
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
	"github.com/daos-stack/daos/src/control/server/storage/scm"
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

func TestDaosServer_prepareNVMe(t *testing.T) {
	// bdev req parameters
	testNrHugePages := 42
	// bdev mock commands
	newPrepCmd := func() *prepareNVMeCmd {
		pdc := &prepareNVMeCmd{
			NrHugepages:  testNrHugePages,
			PCIBlockList: defaultMultiAddrList,
		}
		pdc.Args.PCIAllowList = defaultSingleAddrList
		return pdc
	}

	for name, tc := range map[string]struct {
		prepCmd       *prepareNVMeCmd
		cfg           *config.Server
		bmbc          *bdev.MockBackendConfig
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
				HugePageCount: testNrHugePages,
				PCIAllowList:  defaultSingleAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				EnableVMD:     true,
			},
		},
		"succeeds; different target user; multi allow list": {
			prepCmd: newPrepCmd().WithTargetUser("bob").WithPCIAllowList(defaultMultiAddrList),
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser:    "bob",
				HugePageCount: testNrHugePages,
				PCIAllowList:  spaceSepMultiAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				EnableVMD:     true,
			},
		},
		"fails; user params": {
			prepCmd: newPrepCmd(),
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("backend prep setup failed"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
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
				HugePageCount: testNrHugePages,
				TargetUser:    "root",
				PCIAllowList:  defaultSingleAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				DisableVFIO:   true,
			},
		},
		"config parameters ignored; settings exist": {
			prepCmd: newPrepCmd(),
			cfg: new(config.Server).
				WithEngines(engine.NewConfig().
					WithStorage(storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(8)))).
				WithBdevExclude(test.MockPCIAddr(9)).
				WithNrHugePages(1024),
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				PCIAllowList:  defaultSingleAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				EnableVMD:     true,
			},
		},
		"config parameters ignored; set ignore-config in cmd": {
			prepCmd: new(prepareNVMeCmd).WithIgnoreConfig(true),
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
				WithNrHugePages(1024).
				WithDisableVMD(true),
			expPrepCall: &storage.BdevPrepareRequest{
				EnableVMD: true,
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
				WithNrHugePages(1024).
				WithDisableVMD(true),
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 1024,
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
				WithNrHugePages(1024).
				WithDisableVMD(true),
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: 1024,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7), storage.BdevPciAddrSep,
					test.MockPCIAddr(8)),
				PCIBlockList: test.MockPCIAddr(9),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			msp := scm.NewMockProvider(log, nil, nil)
			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp)

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

			gotErr := tc.prepCmd.prepareNVMe(scs.NvmePrepare)
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
		bmbc          *bdev.MockBackendConfig
		iommuDisabled bool
		expErr        error
		expResetCall  *storage.BdevPrepareRequest
	}{
		"no devices; success": {
			expResetCall: &storage.BdevPrepareRequest{
				EnableVMD: true,
				Reset_:    true,
			},
		},
		"succeeds; user params": {
			resetCmd: newResetCmd(),
			expResetCall: &storage.BdevPrepareRequest{
				PCIAllowList: defaultSingleAddrList,
				PCIBlockList: spaceSepMultiAddrList,
				EnableVMD:    true,
				Reset_:       true,
			},
		},
		"succeeds; different target user; multi allow list": {
			resetCmd: newResetCmd().WithTargetUser("bob").WithPCIAllowList(defaultMultiAddrList),
			expResetCall: &storage.BdevPrepareRequest{
				TargetUser:   "bob",
				PCIAllowList: spaceSepMultiAddrList,
				PCIBlockList: spaceSepMultiAddrList,
				EnableVMD:    true,
				Reset_:       true,
			},
		},
		"fails; user params": {
			resetCmd: newResetCmd(),
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backend prep reset failed"),
			},
			expResetCall: &storage.BdevPrepareRequest{
				PCIAllowList: defaultSingleAddrList,
				PCIBlockList: spaceSepMultiAddrList,
				EnableVMD:    true,
				Reset_:       true,
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
			expResetCall: &storage.BdevPrepareRequest{
				TargetUser:   "root",
				PCIAllowList: defaultSingleAddrList,
				PCIBlockList: spaceSepMultiAddrList,
				DisableVFIO:  true,
				Reset_:       true,
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
				WithNrHugePages(1024),
			expResetCall: &storage.BdevPrepareRequest{
				PCIAllowList: defaultSingleAddrList,
				PCIBlockList: spaceSepMultiAddrList,
				EnableVMD:    true,
				Reset_:       true,
			},
		},
		"config parameters ignored; set ignore-config in cmd": {
			resetCmd: new(resetNVMeCmd).WithIgnoreConfig(true),
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
				WithNrHugePages(1024).
				WithDisableVMD(true),
			expResetCall: &storage.BdevPrepareRequest{
				EnableVMD: true,
				Reset_:    true,
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
				WithNrHugePages(1024).
				WithDisableVMD(true),
			expResetCall: &storage.BdevPrepareRequest{
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7), storage.BdevPciAddrSep,
					test.MockPCIAddr(8)),
				PCIBlockList: test.MockPCIAddr(9),
				Reset_:       true,
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
				WithNrHugePages(1024).
				WithDisableVMD(true),
			expResetCall: &storage.BdevPrepareRequest{
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(7), storage.BdevPciAddrSep,
					test.MockPCIAddr(8)),
				PCIBlockList: test.MockPCIAddr(9),
				Reset_:       true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			msp := scm.NewMockProvider(log, nil, nil)
			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp)

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

			gotErr := tc.resetCmd.resetNVMe(scs.NvmePrepare)
			test.CmpErr(t, tc.expErr, gotErr)

			mbb.RLock()
			if len(mbb.PrepareCalls) != 0 {
				t.Fatalf("unexpected number of prepare calls, want 0 got %d",
					len(mbb.PrepareCalls))
			}
			if tc.expResetCall == nil {
				if len(mbb.ResetCalls) != 0 {
					t.Fatalf("unexpected number of reset calls, want 0 got %d",
						len(mbb.ResetCalls))
				}
			} else {
				if len(mbb.ResetCalls) != 1 {
					t.Fatalf("unexpected number of reset calls, want 1 got %d",
						len(mbb.ResetCalls))
				}
				// If empty TargetUser in cmd, expect current user in call.
				if tc.resetCmd.TargetUser == "" {
					tc.expResetCall.TargetUser = getCurrentUsername(t)
				}
				if diff := cmp.Diff(*tc.expResetCall, mbb.ResetCalls[0]); diff != "" {
					t.Fatalf("unexpected reset calls (-want, +got):\n%s\n", diff)
				}
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
		ignoreCfg     bool
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
		"vmd disabled in cfg; cfg ignored": {
			cfg: &config.Server{
				DisableVMD: &tr,
			},
			ignoreCfg: true,
			expOut:    true,
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
			scanCmd.IgnoreConfig = tc.ignoreCfg
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
		scanCmd     *scanNVMeCmd
		cfg         *config.Server
		ignoreCfg   bool
		bmbc        *bdev.MockBackendConfig
		expErr      error
		expScanCall *storage.BdevScanRequest
	}{
		"normal scan": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
					},
				},
			},
			expScanCall: &storage.BdevScanRequest{},
		},
		"failed scan": {
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("fail"),
			},
			expErr: errors.New("fail"),
		},
		"devices filtered by config": {
			bmbc: &bdev.MockBackendConfig{
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
					WithBdevDeviceList(test.MockPCIAddr(3))),
			),
			expScanCall: &storage.BdevScanRequest{
				DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddr(1),
					test.MockPCIAddr(3)),
			},
		},
		"no devices specified in config": {
			bmbc: &bdev.MockBackendConfig{
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
			),
			expScanCall: &storage.BdevScanRequest{},
		},
		"cfg ignore flag set; device filtering skipped": {
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
						storage.MockNvmeController(2),
						storage.MockNvmeController(3),
					},
				},
			},
			ignoreCfg: true,
			cfg: (&config.Server{}).WithEngines(
				(&engine.Config{}).WithStorage(storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(test.MockPCIAddr(1))),
				(&engine.Config{}).WithStorage(storage.NewTierConfig().
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(test.MockPCIAddr(3))),
			),
			expScanCall: &storage.BdevScanRequest{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			msp := scm.NewMockProvider(log, nil, nil)
			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp)

			if tc.scanCmd == nil {
				tc.scanCmd = &scanNVMeCmd{}
			}
			tc.scanCmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			tc.scanCmd.config = tc.cfg
			tc.scanCmd.IgnoreConfig = tc.ignoreCfg

			gotErr := tc.scanCmd.scanNVMe(scs.NvmeScan)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			mbb.RLock()
			if len(mbb.ScanCalls) != 1 {
				t.Fatalf("unexpected number of scan calls, want 1 got %d", len(mbb.ScanCalls))
			}
			if diff := cmp.Diff(tc.expScanCall, &mbb.ScanCalls[0], cmpopt); diff != "" {
				t.Fatalf("unexpected scan calls (-want, +got):\n%s\n", diff)
			}
			mbb.RUnlock()
		})
	}
}
