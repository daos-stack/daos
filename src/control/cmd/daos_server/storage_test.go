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
	"github.com/daos-stack/daos/src/control/common"
	commands "github.com/daos-stack/daos/src/control/common/storage"
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
	bdevPrepCmd.PciAllowList = fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
		storage.BdevPciAddrSep, common.MockPCIAddr(2))
	bdevPrepCmd.PciBlockList = common.MockPCIAddr(1)
	bdevPrepCmdwVmd := bdevPrepCmd

	for name, tc := range map[string]struct {
		prepCmd      commands.StoragePrepareCmd
		enableVmd    bool
		bmbc         *bdev.MockBackendConfig
		smbc         *scm.MockBackendConfig
		expLogMsg    string
		expErr       error
		expPrepCalls []storage.BdevPrepareRequest
	}{
		"default no devices; success": {},
		"nvme-only no devices; success": {
			prepCmd: commands.StoragePrepareCmd{
				NvmeOnly: true,
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
			smbc: &scm.MockBackendConfig{
				DiscoverRes:         storage.ScmModules{storage.MockScmModule()},
				GetPmemNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
				StartingState:       storage.ScmStateNoCapacity,
			},
		},
		"unprepared scm; warn": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes:   storage.ScmModules{storage.MockScmModule()},
				StartingState: storage.ScmStateNoRegions,
			},
			expErr: errors.New("consent not given"), // prompts for confirmation and gets EOF
		},
		"unprepared scm; force": {
			prepCmd: commands.StoragePrepareCmd{
				Force: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				StartingState:   storage.ScmStateNoRegions,
				PrepNeedsReboot: true,
			},
			expLogMsg: storage.ScmMsgRebootRequired,
		},
		"prepare scm; create namespaces": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes:      storage.ScmModules{storage.MockScmModule()},
				PrepNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
				StartingState:    storage.ScmStateFreeCapacity,
			},
			expLogMsg: printNamespace.String(),
		},
		"reset scm": {
			prepCmd: commands.StoragePrepareCmd{
				Reset: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:      storage.ScmModules{storage.MockScmModule()},
				PrepNamespaceRes: storage.ScmNamespaces{storage.MockScmNamespace()},
				StartingState:    storage.ScmStateNoCapacity,
			},
			expErr: errors.New("consent not given"), // prompts for confirmation and gets EOF
		},
		"scm scan fails": {
			smbc: &scm.MockBackendConfig{
				DiscoverErr: failedErr,
			},
			expErr: failedErr,
		},
		"scm prepare fails": {
			smbc: &scm.MockBackendConfig{
				DiscoverRes:   storage.ScmModules{storage.MockScmModule()},
				StartingState: storage.ScmStateFreeCapacity,
				PrepErr:       failedErr,
			},
			expErr: failedErr,
		},
		"nvme prep succeeds; user params": {
			prepCmd: bdevPrepCmd,
			expPrepCalls: []storage.BdevPrepareRequest{
				// with reset set to false in request, two calls will be made to
				// bdev backend prepare, one to reset device bindings and hugepage
				// allocations and another to set them as requested
				{
					ResetOnly:     true,
					DisableVMD:    true,
					HugePageCount: testNrHugePages,
					TargetUser:    username,
					PciAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PciBlockList: common.MockPCIAddr(1),
				},
				{
					ResetOnly:     false,
					DisableVMD:    true,
					HugePageCount: testNrHugePages,
					TargetUser:    username,
					PciAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PciBlockList: common.MockPCIAddr(1),
				},
			},
		},
		"nvme prep reset fails; user params": {
			prepCmd: bdevPrepCmd,
			bmbc: &bdev.MockBackendConfig{
				PrepareResetErr: errors.New("backed prep reset failed"),
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				// as above but fist call (reset) fails so second is not made
				{
					ResetOnly:     true,
					DisableVMD:    true,
					HugePageCount: testNrHugePages,
					TargetUser:    username,
					PciAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PciBlockList: common.MockPCIAddr(1),
				},
			},
			expErr: errors.New("backed prep reset failed"),
		},
		"nvme prep fails; user params": {
			prepCmd: bdevPrepCmd,
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("backed prep setup failed"),
			},
			expPrepCalls: []storage.BdevPrepareRequest{
				// as above but second call (setup) fails
				{
					ResetOnly:     true,
					DisableVMD:    true,
					HugePageCount: testNrHugePages,
					TargetUser:    username,
					PciAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PciBlockList: common.MockPCIAddr(1),
				},
				{
					ResetOnly:     false,
					DisableVMD:    true,
					HugePageCount: testNrHugePages,
					TargetUser:    username,
					PciAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PciBlockList: common.MockPCIAddr(1),
				},
			},
			expErr: errors.New("backed prep setup failed"),
		},
		"nvme prep succeeds; user params; vmd enabled": {
			prepCmd:   bdevPrepCmdwVmd,
			enableVmd: true,
			expPrepCalls: []storage.BdevPrepareRequest{
				// as above but second call (setup) fails
				{
					DisableVMD:    false,
					ResetOnly:     true,
					HugePageCount: testNrHugePages,
					TargetUser:    username,
					PciAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PciBlockList: common.MockPCIAddr(1),
				},
				{
					DisableVMD:    false,
					ResetOnly:     false,
					HugePageCount: testNrHugePages,
					TargetUser:    username,
					PciAllowList: fmt.Sprintf("%s%s%s", common.MockPCIAddr(1),
						storage.BdevPciAddrSep, common.MockPCIAddr(2)),
					PciBlockList: common.MockPCIAddr(1),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			mbb := bdev.NewMockBackend(tc.bmbc)
			mbp := bdev.NewProvider(log, mbb)

			cmd := &storagePrepareCmd{
				StoragePrepareCmd: tc.prepCmd,
				logCmd: logCmd{
					log: log,
				},
				scs: server.NewMockStorageControlService(log, nil, nil,
					scm.NewMockProvider(log, tc.smbc, nil), mbp),
				EnableVMD: tc.enableVmd,
			}

			gotErr := cmd.Execute(nil)
			if len(tc.expPrepCalls) > 0 {
				if diff := cmp.Diff(tc.expPrepCalls, mbb.PrepareCalls); diff != "" {
					t.Fatalf("unexpected prepare calls (-want, +got):\n%s\n", diff)
				}
			}
			common.CmpErr(t, tc.expErr, gotErr)
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
