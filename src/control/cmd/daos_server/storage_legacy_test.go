//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestDaosServer_StoragePrepare_Legacy(t *testing.T) {
	for name, tc := range map[string]struct {
		legacyCmd        *legacyPrepCmd
		smbc             *scm.MockBackendConfig
		bmbc             *bdev.MockBackendConfig
		iommuDisabled    bool
		expErr           error
		expPrepSCMCall   *storage.ScmPrepareRequest
		expResetSCMCall  *storage.ScmPrepareRequest
		expPrepNVMeCall  *storage.BdevPrepareRequest
		expResetNVMeCall *storage.BdevPrepareRequest
	}{
		"no params; consent not given (scm)": {
			expErr:          errors.New("consent not given"),
			expPrepNVMeCall: &storage.BdevPrepareRequest{EnableVMD: true},
		},
		"force param; unknown state (scm)": {
			legacyCmd:       &legacyPrepCmd{Force: true},
			expErr:          errors.New("failed to report state"),
			expPrepSCMCall:  &storage.ScmPrepareRequest{NrNamespacesPerSocket: 1},
			expPrepNVMeCall: &storage.BdevPrepareRequest{EnableVMD: true},
		},
		"force param; no free capacity (scm)": {
			legacyCmd: &legacyPrepCmd{Force: true},
			smbc: &scm.MockBackendConfig{
				PrepRes: &storage.ScmPrepareResponse{
					Socket:     &storage.ScmSocketState{State: storage.ScmNoFreeCap},
					Namespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
				},
			},
			expPrepSCMCall:  &storage.ScmPrepareRequest{NrNamespacesPerSocket: 1},
			expPrepNVMeCall: &storage.BdevPrepareRequest{EnableVMD: true},
		},
		"set all request params; unknown state (scm) & vfio cannot be disabled (nvme)": {
			legacyCmd: &legacyPrepCmd{
				// SCM params:
				NrNamespacesPerSocket: 2,
				Force:                 true,
				// NVMe params:
				PCIAllowList: defaultMultiAddrList,
				PCIBlockList: defaultSingleAddrList,
				NrHugepages:  9182,
				TargetUser:   "bob",
				DisableVFIO:  true,
			},
			expPrepSCMCall: &storage.ScmPrepareRequest{NrNamespacesPerSocket: 2},
			expErr:         errors.New("storage prepare command returned multiple errors:"),
		},
		"set all request params; no free capacity (scm) & target user is root (nvme)": {
			legacyCmd: &legacyPrepCmd{
				// SCM params:
				NrNamespacesPerSocket: 2,
				Force:                 true,
				// NVMe params:
				PCIAllowList: defaultMultiAddrList,
				PCIBlockList: defaultSingleAddrList,
				NrHugepages:  9182,
				TargetUser:   "root",
				DisableVFIO:  true,
			},
			smbc: &scm.MockBackendConfig{
				PrepRes: &storage.ScmPrepareResponse{
					Socket:     &storage.ScmSocketState{State: storage.ScmNoFreeCap},
					Namespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
				},
			},
			expPrepSCMCall: &storage.ScmPrepareRequest{NrNamespacesPerSocket: 2},
			expPrepNVMeCall: &storage.BdevPrepareRequest{
				TargetUser:    "root",
				HugepageCount: 9182,
				PCIAllowList:  spaceSepMultiAddrList,
				PCIBlockList:  defaultSingleAddrList,
				DisableVFIO:   true,
			},
		},
		"set all request params; scm-only": {
			legacyCmd: &legacyPrepCmd{
				ScmOnly: true,
				// SCM params:
				NrNamespacesPerSocket: 2,
				Force:                 true,
				// NVMe params:
				PCIAllowList: defaultMultiAddrList,
				PCIBlockList: defaultSingleAddrList,
				NrHugepages:  9182,
				TargetUser:   "root",
				DisableVFIO:  true,
			},
			smbc: &scm.MockBackendConfig{
				PrepRes: &storage.ScmPrepareResponse{
					Socket:     &storage.ScmSocketState{State: storage.ScmNoFreeCap},
					Namespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
				},
			},
			expPrepSCMCall: &storage.ScmPrepareRequest{NrNamespacesPerSocket: 2},
		},
		"set all request params; nvme-only": {
			legacyCmd: &legacyPrepCmd{
				NvmeOnly: true,
				// SCM params:
				NrNamespacesPerSocket: 2,
				Force:                 true,
				// NVMe params:
				PCIAllowList: defaultSingleAddrList,
				PCIBlockList: defaultMultiAddrList,
				NrHugepages:  9182,
				TargetUser:   "root",
				DisableVFIO:  true,
			},
			expPrepNVMeCall: &storage.BdevPrepareRequest{
				TargetUser:    "root",
				HugepageCount: 9182,
				PCIAllowList:  defaultSingleAddrList,
				PCIBlockList:  spaceSepMultiAddrList,
				DisableVFIO:   true,
			},
		},
		"set all request params; nvme-only; space-separated address list": {
			legacyCmd: &legacyPrepCmd{
				NvmeOnly:     true,
				PCIAllowList: spaceSepMultiAddrList,
				PCIBlockList: defaultSingleAddrList,
				NrHugepages:  9182,
				TargetUser:   "root",
				DisableVFIO:  true,
			},
			expPrepNVMeCall: &storage.BdevPrepareRequest{
				TargetUser:    "root",
				HugepageCount: 9182,
				PCIAllowList:  spaceSepMultiAddrList,
				PCIBlockList:  defaultSingleAddrList,
				DisableVFIO:   true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)
			msb := scm.NewMockBackend(tc.smbc)
			msp := scm.NewProvider(log, msb, nil, nil)
			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp, nil)

			if tc.legacyCmd == nil {
				tc.legacyCmd = &legacyPrepCmd{}
			}
			tc.legacyCmd.LogCmd = cmdutil.LogCmd{
				Logger: log,
			}
			tc.legacyCmd.setIOMMUChecker(func() (bool, error) {
				return !tc.iommuDisabled, nil
			})
			if tc.legacyCmd.NrNamespacesPerSocket == 0 {
				tc.legacyCmd.NrNamespacesPerSocket = uint(1)
			}

			gotErr := tc.legacyCmd.prep(scs)
			test.CmpErr(t, tc.expErr, gotErr)

			mbb.RLock()
			if tc.expPrepNVMeCall == nil {
				if len(mbb.PrepareCalls) != 0 {
					t.Fatalf("unexpected number of prepare nvme calls, want 0 got %d",
						len(mbb.PrepareCalls))
				}
			} else {
				if len(mbb.PrepareCalls) != 1 {
					t.Fatalf("unexpected number of prepare nvme calls, want 1 got %d",
						len(mbb.PrepareCalls))
				}
				// If empty TargetUser in cmd, expect current user in call.
				if tc.legacyCmd.TargetUser == "" {
					tc.expPrepNVMeCall.TargetUser = getCurrentUsername(t)
				}
				if diff := cmp.Diff(*tc.expPrepNVMeCall, mbb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare nvme calls (-want, +got):\n%s\n", diff)
				}
			}
			if tc.expResetNVMeCall == nil {
				if len(mbb.ResetCalls) != 0 {
					t.Fatalf("unexpected number of reset nvme calls, want 0 got %d",
						len(mbb.ResetCalls))
				}
			} else {
				if len(mbb.ResetCalls) != 1 {
					t.Fatalf("unexpected number of reset nvme calls, want 1 got %d",
						len(mbb.ResetCalls))
				}
				// If empty TargetUser in cmd, expect current user in call.
				if tc.legacyCmd.TargetUser == "" {
					tc.expResetNVMeCall.TargetUser = getCurrentUsername(t)
				}
				if diff := cmp.Diff(*tc.expResetNVMeCall, mbb.ResetCalls[0]); diff != "" {
					t.Fatalf("unexpected reset nvme calls (-want, +got):\n%s\n", diff)
				}
			}
			mbb.RUnlock()

			msb.RLock()
			if tc.expPrepSCMCall == nil {
				if len(msb.PrepareCalls) != 0 {
					t.Fatalf("unexpected number of prepare scm calls, want 0 got %d",
						len(msb.PrepareCalls))
				}
			} else {
				if len(msb.PrepareCalls) != 1 {
					t.Fatalf("unexpected number of prepare scm calls, want 1 got %d",
						len(msb.PrepareCalls))
				}
				if diff := cmp.Diff(*tc.expPrepSCMCall, msb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare scm calls (-want, +got):\n%s\n", diff)
				}
			}
			if tc.expResetSCMCall == nil {
				if len(msb.ResetCalls) != 0 {
					t.Fatalf("unexpected number of reset scm calls, want 0 got %d",
						len(msb.ResetCalls))
				}
			} else {
				if len(msb.ResetCalls) != 1 {
					t.Fatalf("unexpected number of reset scm calls, want 1 got %d",
						len(msb.ResetCalls))
				}
				if diff := cmp.Diff(*tc.expResetSCMCall, msb.ResetCalls[0]); diff != "" {
					t.Fatalf("unexpected reset scm calls (-want, +got):\n%s\n", diff)
				}
			}
			msb.RUnlock()
		})
	}
}
