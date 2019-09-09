//
// (C) Copyright 2019 Intel Corporation.
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
	"fmt"
	"strings"
	"testing"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func TestStorageCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			// FIXME: This arguably should result in an error,
			// but we can't see the io.EOF error because it's
			// swallowed in getConsent()
			"Format without force",
			"storage format",
			"ConnectClients",
			nil,
			cmdSuccess,
		},
		{
			"Format with force",
			"storage format --force",
			"ConnectClients StorageFormat",
			nil,
			cmdSuccess,
		},
		{
			"Update with missing arguments",
			"storage fwupdate",
			"",
			nil,
			errMissingFlag,
		},
		{
			// Likewise here, this should probably result in a failure
			"Update without force",
			"storage fwupdate --nvme-model foo --nvme-fw-path bar --nvme-fw-rev 123",
			"ConnectClients",
			nil,
			cmdSuccess,
		},
		{
			"Update with force",
			"storage fwupdate --force --nvme-model foo --nvme-fw-path bar --nvme-fw-rev 123",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("StorageUpdate-%s", &pb.StorageUpdateReq{
					Nvme: &pb.UpdateNvmeReq{
						Model:    "foo",
						Startrev: "123",
						Path:     "bar",
					},
				}),
			}, " "),
			nil,
			cmdSuccess,
		},
		{
			"Scan",
			"storage scan",
			"ConnectClients StorageScan",
			nil,
			cmdSuccess,
		},
		{
			"Prepare without force",
			"storage prepare",
			"ConnectClients",
			nil,
			fmt.Errorf("consent not given"),
		},
		{
			"Prepare with nvme-only and scm-only",
			"storage prepare --force --nvme-only --scm-only",
			"ConnectClients",
			nil,
			fmt.Errorf("nvme-only and scm-only options should not be set together"),
		},
		{
			"Prepare with scm-only",
			"storage prepare --force --scm-only",
			"ConnectClients StoragePrepare",
			nil,
			cmdSuccess,
		},
		{
			"Prepare with nvme-only",
			"storage prepare --force --nvme-only",
			"ConnectClients StoragePrepare",
			nil,
			cmdSuccess,
		},
		{
			"Prepare with non-existent option",
			"storage prepare --force --nvme",
			"",
			nil,
			fmt.Errorf("unknown flag `nvme'"),
		},
		{
			"Prepare with force and reset",
			"storage prepare --force --reset",
			"ConnectClients StoragePrepare",
			nil,
			cmdSuccess,
		},
		{
			"Prepare with force",
			"storage prepare --force",
			"ConnectClients StoragePrepare",
			nil,
			cmdSuccess,
		},
		{
			"Nonexistent subcommand",
			"storage quack",
			"",
			nil,
			fmt.Errorf("Unknown command"),
		},
	})
}
