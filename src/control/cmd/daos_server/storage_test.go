//
// (C) Copyright 2020-2021 Intel Corporation.
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
	commands "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestDaosServer_StoragePrepare(t *testing.T) {
	failedErr := errors.New("it failed")
	var printNamespace strings.Builder
	msns := storage.ScmNamespaces{storage.MockScmNamespace()}
	if err := pretty.PrintScmNamespaces(msns, &printNamespace); err != nil {
		t.Fatal(err)
	}
	// bdev req parameters
	testNrHugePages := 42
	usrCurrent, _ := user.Current()
	username := usrCurrent.Username
	// bdev mock commands
	bdevPrepCmd := commands.StoragePrepareCmd{
		NvmeOnly: true,
	}
	bdevPrepCmd.NrHugepages = testNrHugePages
	bdevPrepCmd.TargetUser = username
	bdevPrepCmd.PCIAllowList = fmt.Sprintf("%s%s%s", test.MockPCIAddr(1),
		storage.BdevPciAddrSep, test.MockPCIAddr(2))
	bdevPrepCmd.PCIBlockList = test.MockPCIAddr(1)
	bdevResetCmd := bdevPrepCmd
	bdevResetCmd.Reset = true

	for name, tc := range map[string]struct {
		prepCmd      commands.StoragePrepareCmd
		bmbc         *bdev.MockBackendConfig
		smbc         *scm.MockBackendConfig
		expLogMsg    string
		expErr       error
		expPrepCall  *storage.BdevPrepareRequest
		expResetCall *storage.BdevPrepareRequest
	}{
		"default no devices; success": {
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser: username,
				// always set in local storage prepare to allow automatic detection
				EnableVMD: true,
			},
		},
		"nvme-only no devices; success": {
			prepCmd: commands.StoragePrepareCmd{
				NvmeOnly: true,
			},
			expPrepCall: &storage.BdevPrepareRequest{
				TargetUser: username,
				EnableVMD:  true,
			},
		},
		"scm-only no devices; success": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly: true,
			},
		},
		"setting nvme-only and scm-only should fail": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly:  true,
				NvmeOnly: true,
			},
			expErr: errors.New("should not be set"),
		},
		"prepared scm; success": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
				StartingState:       storage.ScmStateNoCapacity,
			},
		},
		"unprepared scm; warn": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:   storage.ScmModules{storage.MockScmModule()},
				StartingState: storage.ScmStateNoRegions,
			},
			expErr: errors.New("consent not given"), // prompts for confirmation and gets EOF
		},
		"unprepared scm; force": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly: true,
				Force:   true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				StartingState:   storage.ScmStateNoRegions,
				PrepNeedsReboot: true,
			},
			expLogMsg: storage.ScmMsgRebootRequired,
		},
		"prepare scm; create namespaces": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly: true,
				Force:   true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:      storage.ScmModules{storage.MockScmModule()},
				PrepNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
				StartingState:    storage.ScmStateFreeCapacity,
			},
			expLogMsg: printNamespace.String(),
		},
		"reset scm": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly: true,
				Reset:   true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:      storage.ScmModules{storage.MockScmModule()},
				PrepNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
				StartingState:    storage.ScmStateNoCapacity,
			},
			expErr: errors.New("consent not given"), // prompts for confirmation and gets EOF
		},
		"scm scan fails": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: failedErr,
			},
			expErr: failedErr,
		},
		"scm prepare fails": {
			prepCmd: commands.StoragePrepareCmd{
				ScmOnly: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:   storage.ScmModules{storage.MockScmModule()},
				StartingState: storage.ScmStateFreeCapacity,
				PrepErr:       failedErr,
			},
			expErr: failedErr,
		},
		"nvme prep succeeds; user params": {
			prepCmd: bdevPrepCmd,
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
			prepCmd: bdevPrepCmd,
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

			cmd := &storagePrepareCmd{
				StoragePrepareCmd: tc.prepCmd,
				logCmd: logCmd{
					log: log,
				},
				scs: server.NewMockStorageControlService(log, nil, nil,
					scm.NewMockProvider(log, tc.smbc, nil), mbp),
			}

			gotErr := cmd.Execute(nil)

			mbb.RLock()
			if tc.expPrepCall == nil {
				if len(mbb.PrepareCalls) != 0 {
					t.Fatal("unexpected number of prepared calls")
				}
			} else {
				if len(mbb.PrepareCalls) != 1 {
					t.Fatal("unexpected number of prepared calls")
				}
				if diff := cmp.Diff(*tc.expPrepCall, mbb.PrepareCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
				}
			}
			mbb.RUnlock()

			mbb.RLock()
			if tc.expResetCall == nil {
				if len(mbb.ResetCalls) != 0 {
					t.Fatal("unexpected number of prepared calls")
				}
			} else {
				if len(mbb.ResetCalls) != 1 {
					t.Fatal("unexpected number of prepared calls")
				}
				if diff := cmp.Diff(*tc.expResetCall, mbb.ResetCalls[0]); diff != "" {
					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
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
