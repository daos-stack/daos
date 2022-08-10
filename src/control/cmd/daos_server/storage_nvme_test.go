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

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

var currentUsername string

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

func TestDaosServer_StoragePrepare_NVMe(t *testing.T) {
	// bdev req parameters
	testNrHugePages := 42
	// bdev mock commands
	newPrepCmd := func() *prepareDrivesCmd {
		return &prepareDrivesCmd{
			NrHugepages: testNrHugePages,
			PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
				storage.BdevPciAddrSep, test.MockPCIAddr(2)),
			PCIBlockList: test.MockPCIAddr(1),
		}
	}

	for name, tc := range map[string]struct {
		prepCmd       *prepareDrivesCmd
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
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
		},
		"succeeds; different target user": {
			prepCmd: newPrepCmd().WithTargetUser("bob"),
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser:    "bob",
				HugePageCount: testNrHugePages,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
		},
		"fails; user params": {
			prepCmd: newPrepCmd(),
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("backend prep setup failed"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
			expErr: errors.New("backend prep setup failed"),
		},
		"non-root; vfio disabled": {
			prepCmd: newPrepCmd().WithDisableVFIO(true),
			expErr:  errors.New("VFIO can not be disabled"),
		},
		"non-root; iommu not detected": {
			iommuDisabled: true,
			expErr:        errors.New("no IOMMU detected"),
		},
		"root; vfio disabled; iommu not detected": {
			prepCmd:       newPrepCmd().WithTargetUser("root").WithDisableVFIO(true),
			iommuDisabled: true,
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    "root",
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				DisableVFIO:  true,
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
				tc.prepCmd = &prepareDrivesCmd{}
			}
			tc.prepCmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}

			gotErr := tc.prepCmd.prepareNVMe(scs.NvmePrepare, !tc.iommuDisabled)
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
	newRelCmd := func() *releaseDrivesCmd {
		return &releaseDrivesCmd{
			PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
				storage.BdevPciAddrSep, test.MockPCIAddr(2)),
			PCIBlockList: test.MockPCIAddr(1),
		}
	}

	for name, tc := range map[string]struct {
		relCmd        *releaseDrivesCmd
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
			relCmd: newRelCmd(),
			expResetCall: &storage.BdevPrepareRequest{
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
				Reset_:       true,
			},
		},
		"succeeds; different target user": {
			relCmd: newRelCmd().WithTargetUser("bob"),
			expResetCall: &storage.BdevPrepareRequest{
				TargetUser: "bob",
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
				Reset_:       true,
			},
		},
		"fails; user params": {
			relCmd: newRelCmd(),
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backend prep reset failed"),
			},
			expResetCall: &storage.BdevPrepareRequest{
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
				Reset_:       true,
			},
			expErr: errors.New("backend prep reset failed"),
		},
		"non-root; vfio disabled": {
			relCmd: newRelCmd().WithDisableVFIO(true),
			expErr: errors.New("VFIO can not be disabled"),
		},
		"non-root; iommu not detected": {
			iommuDisabled: true,
			expErr:        errors.New("no IOMMU detected"),
		},
		"root; vfio disabled; iommu not detected": {
			relCmd:        newRelCmd().WithTargetUser("root").WithDisableVFIO(true),
			iommuDisabled: true,
			expResetCall: &storage.BdevPrepareRequest{
				TargetUser: "root",
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				DisableVFIO:  true,
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

			if tc.relCmd == nil {
				tc.relCmd = &releaseDrivesCmd{}
			}
			tc.relCmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}

			gotErr := tc.relCmd.resetNVMe(scs.NvmePrepare, !tc.iommuDisabled)
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
						len(mbb.PrepareCalls))
				}
				// If empty TargetUser in cmd, expect current user in call.
				if tc.relCmd.TargetUser == "" {
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
