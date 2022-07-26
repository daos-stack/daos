//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

//func TestDaosServer_StoragePrepare_NVMe(t *testing.T) {
//	// bdev req parameters
//	testNrHugePages := 42
//	usrCurrent, _ := user.Current()
//	username := usrCurrent.Username
//	// bdev mock commands
//	newBdevPrepCmd := func() *storagePrepNVMeCmd {
//		return &storagePrepNVMeCmd{
//			NrHugepages: testNrHugePages,
//			TargetUser:  username,
//			PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
//				storage.BdevPciAddrSep, test.MockPCIAddr(2)),
//			PCIBlockList: test.MockPCIAddr(1),
//		}
//	}
//	bdevResetCmd := newBdevPrepCmd()
//	bdevResetCmd.Reset = true
//
//	for name, tc := range map[string]struct {
//		prepCmd       *storagePrepNVMeCmd
//		bmbc          *bdev.MockBackendConfig
//		iommuDisabled bool
//		expErr        error
//		expPrepCall   *storage.BdevPrepareRequest
//		expResetCall  *storage.BdevPrepareRequest
//	}{
//		"no devices; success": {
//			expPrepCall: &storage.BdevPrepareRequest{
//				TargetUser: username,
//				// always set in local storage prepare to allow automatic detection
//				EnableVMD: true,
//			},
//		},
//		"nvme prep succeeds; user params": {
//			prepCmd: newBdevPrepCmd(),
//			expPrepCall: &storage.BdevPrepareRequest{
//				HugePageCount: testNrHugePages,
//				TargetUser:    username,
//				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
//					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
//				PCIBlockList: test.MockPCIAddr(1),
//				EnableVMD:    true,
//			},
//		},
//		"nvme prep fails; user params": {
//			prepCmd: newBdevPrepCmd(),
//			bmbc: &bdev.MockBackendConfig{
//				PrepareErr: errors.New("backed prep setup failed"),
//			},
//			expPrepCall: &storage.BdevPrepareRequest{
//				HugePageCount: testNrHugePages,
//				TargetUser:    username,
//				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
//					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
//				PCIBlockList: test.MockPCIAddr(1),
//				EnableVMD:    true,
//			},
//			expErr: errors.New("backed prep setup failed"),
//		},
//		"non-root; vfio disabled": {
//			prepCmd: newBdevPrepCmd().WithDisableVFIO(true),
//			expErr:  errors.New("VFIO can not be disabled"),
//		},
//		"non-root; iommu not detected": {
//			iommuDisabled: true,
//			expErr:        errors.New("no IOMMU detected"),
//		},
//		"root; vfio disabled; iommu not detected": {
//			prepCmd:       newBdevPrepCmd().WithTargetUser("root").WithDisableVFIO(true),
//			iommuDisabled: true,
//			expPrepCall: &storage.BdevPrepareRequest{
//				HugePageCount: testNrHugePages,
//				TargetUser:    "root",
//				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
//					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
//				PCIBlockList: test.MockPCIAddr(1),
//				DisableVFIO:  true,
//			},
//		},
//		"nvme prep reset succeeds; user params": {
//			prepCmd: bdevResetCmd,
//			expResetCall: &storage.BdevPrepareRequest{
//				Reset_:        true,
//				HugePageCount: testNrHugePages,
//				TargetUser:    username,
//				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
//					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
//				PCIBlockList: test.MockPCIAddr(1),
//				EnableVMD:    true,
//			},
//		},
//		"nvme prep reset fails; user params": {
//			prepCmd: bdevResetCmd,
//			bmbc: &bdev.MockBackendConfig{
//				ResetErr: errors.New("backed prep reset failed"),
//			},
//			expResetCall: &storage.BdevPrepareRequest{
//				Reset_:        true,
//				HugePageCount: testNrHugePages,
//				TargetUser:    username,
//				PCIAllowList: fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
//					storage.BdevPciAddrSep, test.MockPCIAddr(2)),
//				PCIBlockList: test.MockPCIAddr(1),
//				EnableVMD:    true,
//			},
//			expErr: errors.New("backed prep reset failed"),
//		},
//	} {
//		t.Run(name, func(t *testing.T) {
//			log, buf := logging.NewTestLogger(name)
//			defer test.ShowBufferOnFailure(t, buf)
//
//			mbb := bdev.NewMockBackend(tc.bmbc)
//			mbp := bdev.NewProvider(log, mbb)
//			msp := scm.NewMockProvider(log, nil, nil)
//			scs := server.NewMockStorageControlService(log, nil, nil, msp, mbp)
//
//			if tc.prepCmd == nil {
//				tc.prepCmd = &storagePrepNVMeCmd{}
//			}
//			tc.prepCmd.LogCmd = cmdutil.LogCmd{
//				Logger: log,
//			}
//
//			gotErr := tc.prepCmd.prepNVMe(scs.NvmePrepare, !tc.iommuDisabled)
//
//			mbb.RLock()
//			if tc.expPrepCall == nil {
//				if len(mbb.PrepareCalls) != 0 {
//					t.Fatalf("unexpected number of prepare calls, want 0 got %d",
//						len(mbb.PrepareCalls))
//				}
//			} else {
//				if len(mbb.PrepareCalls) != 1 {
//					t.Fatalf("unexpected number of prepare calls, want 1 got %d",
//						len(mbb.PrepareCalls))
//				}
//				if diff := cmp.Diff(*tc.expPrepCall, mbb.PrepareCalls[0]); diff != "" {
//					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
//				}
//			}
//			mbb.RUnlock()
//
//			mbb.RLock()
//			if tc.expResetCall == nil {
//				if len(mbb.ResetCalls) != 0 {
//					t.Fatalf("unexpected number of reset calls, want 0 got %d",
//						len(mbb.ResetCalls))
//				}
//			} else {
//				if len(mbb.ResetCalls) != 1 {
//					t.Fatalf("unexpected number of reset calls, want 1 got %d",
//						len(mbb.PrepareCalls))
//				}
//				if diff := cmp.Diff(*tc.expResetCall, mbb.ResetCalls[0]); diff != "" {
//					t.Fatalf("unexpected reset calls (-want, +got):\n%s\n", diff)
//				}
//			}
//			mbb.RUnlock()
//
//			test.CmpErr(t, tc.expErr, gotErr)
//			if tc.expErr != nil {
//				return
//			}
//		})
//	}
//}
