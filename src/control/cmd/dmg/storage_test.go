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
	"testing"

	"github.com/google/go-cmp/cmp"

	. "github.com/daos-stack/daos/src/control/client"
	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestStorageCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Format without reformat",
			"storage format",
			"ConnectClients StorageFormat-false",
			nil,
		},
		{
			"Format with reformat",
			"storage format --reformat",
			"ConnectClients StorageFormat-true",
			nil,
		},
		{
			"Scan",
			"storage scan",
			"ConnectClients StorageScan-<nil>",
			nil,
		},
		{
			"Prepare without force",
			"storage prepare",
			"ConnectClients",
			fmt.Errorf("consent not given"),
		},
		{
			"Prepare with nvme-only and scm-only",
			"storage prepare --force --nvme-only --scm-only",
			"ConnectClients",
			fmt.Errorf("nvme-only and scm-only options should not be set together"),
		},
		{
			"Prepare with scm-only",
			"storage prepare --force --scm-only",
			"ConnectClients StoragePrepare",
			nil,
		},
		{
			"Prepare with nvme-only",
			"storage prepare --force --nvme-only",
			"ConnectClients StoragePrepare",
			nil,
		},
		{
			"Prepare with non-existent option",
			"storage prepare --force --nvme",
			"",
			fmt.Errorf("unknown flag `nvme'"),
		},
		{
			"Prepare with force and reset",
			"storage prepare --force --reset",
			"ConnectClients StoragePrepare",
			nil,
		},
		{
			"Prepare with force",
			"storage prepare --force",
			"ConnectClients StoragePrepare",
			nil,
		},
		{
			"Nonexistent subcommand",
			"storage quack",
			"",
			fmt.Errorf("Unknown command"),
		},
	})
}

func TestScanDisplay(t *testing.T) {
	mockController := storage.MockNvmeController()

	for name, tc := range map[string]struct {
		scanResp  *StorageScanResp
		summary   bool
		expOut    string
		expErrMsg string
	}{
		"typical scan": {
			scanResp: MockScanResp(MockCtrlrs, MockScmModules, MockScmNamespaces, MockServers),
			expOut: fmt.Sprintf("\n 1.2.3.[4-5]\n\tSCM Namespaces:\n\t\tDevice:pmem1 Socket:1 "+
				"Capacity:2.90TB\n\tNVMe controllers and namespaces:\n\t\t"+
				"PCI:%s Model:%s FW:%s Socket:%d Capacity:%d.00GB\n",
				mockController.PciAddr, mockController.Model, mockController.FwRev,
				mockController.SocketID, mockController.Namespaces[0].Size),
		},
		"summary scan": {
			scanResp: MockScanResp(MockCtrlrs, MockScmModules, MockScmNamespaces, MockServers),
			summary:  true,
			expOut: fmt.Sprintf("\n HOSTS\t\tSCM\t\t\tNVME\t\n -----\t\t---\t\t\t----\t\n 1.2"+
				".3.[4-5]\t2.90TB (1 namespace)\t%d.00GB (1 controller)",
				mockController.Namespaces[0].Size),
		},
		"scm scan with pmem namespaces": {
			scanResp: MockScanResp(nil, MockScmModules, MockScmNamespaces, MockServers),
			expOut: "\n 1.2.3.[4-5]\n\tSCM Namespaces:\n\t\tDevice:pmem1 Socket:1 " +
				"Capacity:2.90TB\n\tNVMe controllers and namespaces:\n\t\tnone\n",
		},
		"summary scm scan with pmem namespaces": {
			scanResp: MockScanResp(nil, MockScmModules, MockScmNamespaces, MockServers),
			summary:  true,
			expOut: "\n HOSTS\t\tSCM\t\t\tNVME\t\n -----\t\t---\t\t\t----\t\n 1.2.3." +
				"[4-5]\t2.90TB (1 namespace)\t0.00B (0 controllers)",
		},
		"scm scan without pmem namespaces": {
			scanResp: MockScanResp(nil, MockScmModules, nil, MockServers),
			expOut: "\n 1.2.3.[4-5]\n\tSCM Modules:\n\t\tPhysicalID:12345 " +
				"Capacity:12.06KB Location:(socket:4 memctrlr:3 chan:1 pos:2)\n" +
				"\tNVMe controllers and namespaces:\n\t\tnone\n",
		},
		"summary scm scan without pmem namespaces": {
			scanResp: MockScanResp(nil, MockScmModules, nil, MockServers),
			summary:  true,
			expOut: "\n HOSTS\t\tSCM\t\t\tNVME\t\n -----\t\t---\t\t\t----\t\n 1.2.3." +
				"[4-5]\t12.06KB (1 module)\t0.00B (0 controllers)",
		},
		"nvme scan": {
			scanResp: MockScanResp(MockCtrlrs, nil, nil, MockServers),
			expOut: fmt.Sprintf("\n 1.2.3.[4-5]\n\tSCM Modules:\n\t\tnone\n\t"+
				"NVMe controllers and namespaces:\n\t\t"+
				"PCI:%s Model:%s FW:%s Socket:%d Capacity:%d.00GB\n",
				mockController.PciAddr, mockController.Model, mockController.FwRev,
				mockController.SocketID, mockController.Namespaces[0].Size),
		},
		"summary nvme scan": {
			scanResp: MockScanResp(MockCtrlrs, nil, nil, MockServers),
			summary:  true,
			expOut: fmt.Sprintf("\n HOSTS\t\tSCM\t\t\tNVME\t\n -----\t\t---\t\t\t----\t\n 1.2"+
				".3.[4-5]\t0.00B (0 modules)\t%d.00GB (1 controller)",
				mockController.Namespaces[0].Size),
		},
	} {
		t.Run(name, func(t *testing.T) {
			out, err := scanCmdDisplay(tc.scanResp, tc.summary)
			ExpectError(t, err, tc.expErrMsg, name)
			if diff := cmp.Diff(tc.expOut, out); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
