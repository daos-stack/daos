//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//
package main

import (
	"strings"
	"testing"

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

	for name, tc := range map[string]struct {
		nvmeOnly  bool
		scmOnly   bool
		reset     bool
		force     bool
		bmbc      *bdev.MockBackendConfig
		smbc      *scm.MockBackendConfig
		expLogMsg string
		expErr    error
	}{
		"default no devices; success": {},
		"nvme-only no devices; success": {
			nvmeOnly: true,
		},
		"scm-only no devices; success": {
			scmOnly: true,
		},
		"setting nvme-only and scm-only should fail": {
			nvmeOnly: true,
			scmOnly:  true,
			expErr:   errors.New("should not be set"),
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
			force: true,
			smbc: &scm.MockBackendConfig{
				DiscoverRes:     storage.ScmModules{storage.MockScmModule()},
				StartingState:   storage.ScmStateNoRegions,
				PrepNeedsReboot: true,
			},
			expLogMsg: scm.MsgRebootRequired,
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
			reset: true,
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
		"nvme prep fails": {
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: failedErr,
			},
			expErr: failedErr,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			bdevProvider := bdev.NewMockProvider(log, tc.bmbc)
			scmProvider := scm.NewMockProvider(log, tc.smbc, nil)
			cmd := &storagePrepareCmd{
				StoragePrepareCmd: commands.StoragePrepareCmd{
					NvmeOnly: tc.nvmeOnly,
					ScmOnly:  tc.scmOnly,
					Reset:    tc.reset,
					Force:    tc.force,
				},
				logCmd: logCmd{
					log: log,
				},
				scs: server.NewStorageControlService(log, bdevProvider, scmProvider, nil),
			}

			gotErr := cmd.Execute(nil)
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
