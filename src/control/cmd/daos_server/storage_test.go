//
// (C) Copyright 2020-2022 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	commands "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestDaosServer_StoragePrepare_SCM(t *testing.T) {
	var printNamespace strings.Builder
	msns := storage.ScmNamespaces{storage.MockScmNamespace()}
	if err := pretty.PrintScmNamespaces(msns, &printNamespace); err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		noForce    bool
		zeroNrNs   bool
		reset      bool
		prepResp   *storage.ScmPrepareResponse
		prepErr    error
		expErr     error
		expScanErr error
		expLogMsg  string
	}{
		"no modules": {
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoModules,
			},
			expScanErr: storage.FaultScmNoModules,
		},
		"prepare fails": {
			prepErr:    errors.New("fail"),
			expScanErr: errors.New("fail"),
		},
		"create regions; no consent": {
			noForce: true,
			// prompts for confirmation and gets EOF
			expScanErr: errors.New("consent not given"),
		},
		"create regions; no state change": {
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoRegions,
			},
			expScanErr: errors.New("failed to create regions"),
		},
		"create regions; reboot required": {
			prepResp: &storage.ScmPrepareResponse{
				State:          storage.ScmStateNoRegions,
				RebootRequired: true,
			},
			expLogMsg: storage.ScmMsgRebootRequired,
		},
		"non-interleaved regions": {
			// If non-interleaved regions are detected, prep will return an
			// error. So returning the state is unexpected.
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNotInterleaved,
			},
			expScanErr: errors.New("unexpected state"),
		},
		"invalid number of namespaces per socket": {
			zeroNrNs: true,
			expErr:   errors.New("at least 1"),
		},
		"create namespaces; no state change": {
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateFreeCapacity,
			},
			expScanErr: errors.New("failed to create namespaces"),
		},
		"create namespaces; no namespaces reported": {
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoFreeCapacity,
			},
			expScanErr: errors.New("failed to find namespaces"),
		},
		"create namespaces; namespaces reported": {
			prepResp: &storage.ScmPrepareResponse{
				State:      storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expLogMsg: printNamespace.String(),
		},
		"reset; remove regions; no consent": {
			reset:   true,
			noForce: true,
			// prompts for confirmation and gets EOF
			expScanErr: errors.New("consent not given"),
		},
		"reset; remove regions; reboot required": {
			reset: true,
			prepResp: &storage.ScmPrepareResponse{
				State:          storage.ScmStateNoRegions,
				RebootRequired: true,
			},
			expLogMsg: storage.ScmMsgRebootRequired,
		},
		"reset; no regions": {
			reset: true,
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoRegions,
			},
			expLogMsg: "reset successful",
		},
		"reset; regions not interleaved": {
			reset: true,
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNotInterleaved,
			},
			expScanErr: errors.New("unexpected state"),
		},
		"reset; free capacity": {
			reset: true,
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateFreeCapacity,
			},
			expScanErr: errors.New("unexpected state"),
		},
		"reset; no free capacity": {
			reset: true,
			prepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoFreeCapacity,
			},
			expScanErr: errors.New("unexpected state"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			spc := commands.StoragePrepareCmd{
				ScmOnly: true,
				Force:   !tc.noForce,
				Reset:   tc.reset,
			}
			nrNs := uint(1)
			if tc.zeroNrNs {
				nrNs = 0
			}
			spc.NrNamespacesPerSocket = nrNs

			cmd := &storagePrepareCmd{
				StoragePrepareCmd: spc,
				LogCmd: cmdutil.LogCmd{
					Logger: log,
				},
			}
			scanErrors := make(errs, 0, 2)
			mockScmPrep := func(storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error) {
				return tc.prepResp, tc.prepErr
			}

			err := cmd.prepScm(&scanErrors, mockScmPrep)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			var se error
			if len(scanErrors) > 0 {
				se = common.ConcatErrors(scanErrors, nil)
			}
			test.CmpErr(t, tc.expScanErr, se)

			if tc.expLogMsg != "" {
				if !strings.Contains(buf.String(), tc.expLogMsg) {
					t.Fatalf("expected to see %q in log, got %q",
						tc.expLogMsg, buf.String())
				}
			}
		})
	}
}

func TestDaosServer_StoragePrepare_NVMe(t *testing.T) {
	// bdev req parameters
	testNrHugePages := 42
	usrCurrent, _ := user.Current()
	username := usrCurrent.Username
	// bdev mock commands
	newBdevPrepCmd := func() *commands.StoragePrepareCmd {
		cmd := &commands.StoragePrepareCmd{
			NvmeOnly: true,
		}
		cmd.NrHugepages = testNrHugePages
		cmd.TargetUser = username
		cmd.PCIAllowList = fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
			storage.BdevPciAddrSep, test.MockPCIAddr(2))
		cmd.PCIBlockList = test.MockPCIAddr(1)
		return cmd
	}
	bdevResetCmd := newBdevPrepCmd()
	bdevResetCmd.Reset = true

	for name, tc := range map[string]struct {
		prepCmd       *commands.StoragePrepareCmd
		bmbc          *bdev.MockBackendConfig
		iommuDisabled bool
		expLogMsg     string
		expErr        error
		expPrepCall   *storage.BdevPrepareRequest
		expResetCall  *storage.BdevPrepareRequest
	}{
		"no devices; success": {
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser: username,
				// always set in local storage prepare to allow automatic detection
				EnableVMD: true,
			},
		},
		"setting nvme-only and scm-only should fail": {
			prepCmd: &commands.StoragePrepareCmd{
				ScmOnly:  true,
				NvmeOnly: true,
			},
			expErr: errors.New("should not be set"),
		},
		"nvme prep succeeds; user params": {
			prepCmd: newBdevPrepCmd(),
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
		},
		"nvme prep fails; user params": {
			prepCmd: newBdevPrepCmd(),
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("backed prep setup failed"),
			},
			expPrepCall: &storage.BdevPrepareRequest{
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
			expErr: errors.New("backed prep setup failed"),
		},
		"non-root; vfio disabled": {
			prepCmd: newBdevPrepCmd().WithDisableVFIO(true),
			expErr:  errors.New("VFIO can not be disabled"),
		},
		"non-root; iommu not detected": {
			iommuDisabled: true,
			expErr:        errors.New("no IOMMU detected"),
		},
		"root; vfio disabled; iommu not detected": {
			prepCmd:       newBdevPrepCmd().WithTargetUser("root").WithDisableVFIO(true),
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
		"nvme prep reset succeeds; user params": {
			prepCmd: bdevResetCmd,
			expResetCall: &storage.BdevPrepareRequest{
				Reset_:        true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
		},
		"nvme prep reset fails; user params": {
			prepCmd: bdevResetCmd,
			bmbc: &bdev.MockBackendConfig{
				ResetErr: errors.New("backed prep reset failed"),
			},
			expResetCall: &storage.BdevPrepareRequest{
				Reset_:        true,
				HugePageCount: testNrHugePages,
				TargetUser:    username,
				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
				PCIBlockList: test.MockPCIAddr(1),
				EnableVMD:    true,
			},
			expErr: errors.New("backed prep reset failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)

			if tc.prepCmd == nil {
				tc.prepCmd = &commands.StoragePrepareCmd{}
			}
			tc.prepCmd.NrNamespacesPerSocket = 1 // default applied by goflags

			cmd := &storagePrepareCmd{
				StoragePrepareCmd: *tc.prepCmd,
				LogCmd: cmdutil.LogCmd{
					Logger: log,
				},
				scs: server.NewMockStorageControlService(log, nil, nil,
					scm.NewMockProvider(log, nil, nil), mbp),
			}

			gotErr := cmd.prepNvme(cmd.scs.NvmePrepare, !tc.iommuDisabled)

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
				if diff := cmp.Diff(*tc.expPrepCall, mbb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
				}
			}
			mbb.RUnlock()

			mbb.RLock()
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
				if diff := cmp.Diff(*tc.expResetCall, mbb.ResetCalls[0]); diff != "" {
					t.Fatalf("unexpected reset calls (-want, +got):\n%s\n", diff)
				}
			}
			mbb.RUnlock()

			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if tc.expLogMsg != "" {
				if !strings.Contains(buf.String(), tc.expLogMsg) {
					t.Fatalf("expected to see %q in log, got %q",
						tc.expLogMsg, buf.String())
				}
			}
		})
	}
}
