//
// (C) Copyright 2018-2019 Intel Corporation.
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

	. "github.com/daos-stack/daos/src/control/client"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/google/go-cmp/cmp"
)

func TestHasConnection(t *testing.T) {
	var shelltests = []struct {
		results ResultMap
		out     string
	}{
		{
			ResultMap{},
			"Active connections: []\nNo active connections!",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, nil}},
			"Active connections: [1.2.3.4:10000]\n",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, MockErr}},
			"failed to connect to 1.2.3.4:10000 (unknown failure)\nActive connections: []\nNo active connections!",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, nil}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, nil}},
			"Active connections: [1.2.3.4:10000 1.2.3.5:10001]\n",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, MockErr}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, MockErr}},
			"failed to connect to 1.2.3.4:10000 (unknown failure)\nfailed to connect to 1.2.3.5:10001 (unknown failure)\nActive connections: []\nNo active connections!",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, MockErr}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, nil}},
			"failed to connect to 1.2.3.4:10000 (unknown failure)\nActive connections: [1.2.3.5:10001]\n",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, nil}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, MockErr}},
			"failed to connect to 1.2.3.5:10001 (unknown failure)\nActive connections: [1.2.3.4:10000]\n",
		},
	}

	for _, tt := range shelltests {
		_, out := hasConns(tt.results)
		if diff := cmp.Diff(out, tt.out); diff != "" {
			t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
		}
	}
}

func TestCheckSprint(t *testing.T) {
	for name, tt := range map[string]struct {
		m   string
		out string
	}{
		"nvme scan without health": {
			fmt.Sprint(MockNvmeScanResults(MockCtrlrs, MockServers, false)),
			"map[1.2.3.4:10000:NVMe SSD controller and constituent namespaces:\n\tPCI Addr:0000:81:00.0 Serial:123ABC Model:ABC Fwrev:1.0.0 Socket:0\n\t\tNamespace: id:12345 capacity:97.66KB\n 1.2.3.5:10001:NVMe SSD controller and constituent namespaces:\n\tPCI Addr:0000:81:00.0 Serial:123ABC Model:ABC Fwrev:1.0.0 Socket:0\n\t\tNamespace: id:12345 capacity:97.66KB\n]",
		},
		"nvme scan with health": {
			fmt.Sprint(MockNvmeScanResults(MockCtrlrs, MockServers, true)),
			"map[1.2.3.4:10000:NVMe SSD controller, constituent namespaces and health statistics:\n\tPCI Addr:0000:81:00.0 Serial:123ABC Model:ABC Fwrev:1.0.0 Socket:0\n\t\tNamespace: id:12345 capacity:97.66KB\n\tHealth Stats:\n\t\tTemperature:300K(27C)\n\t\tController Busy Time:0 minutes\n\t\tPower Cycles:99\n\t\tPower On Hours:9999 hours\n\t\tUnsafe Shutdowns:1\n\t\tMedia Errors:0\n\t\tError Log Entries:0\n\t\tCritical Warnings:\n\t\t\tTemperature: OK\n\t\t\tAvailable Spare: OK\n\t\t\tDevice Reliability: OK\n\t\t\tRead Only: OK\n\t\t\tVolatile Memory Backup: OK\n 1.2.3.5:10001:NVMe SSD controller, constituent namespaces and health statistics:\n\tPCI Addr:0000:81:00.0 Serial:123ABC Model:ABC Fwrev:1.0.0 Socket:0\n\t\tNamespace: id:12345 capacity:97.66KB\n\tHealth Stats:\n\t\tTemperature:300K(27C)\n\t\tController Busy Time:0 minutes\n\t\tPower Cycles:99\n\t\tPower On Hours:9999 hours\n\t\tUnsafe Shutdowns:1\n\t\tMedia Errors:0\n\t\tError Log Entries:0\n\t\tCritical Warnings:\n\t\t\tTemperature: OK\n\t\t\tAvailable Spare: OK\n\t\t\tDevice Reliability: OK\n\t\t\tRead Only: OK\n\t\t\tVolatile Memory Backup: OK\n]",
		},
		"scm scan with pmem namespaces": {
			fmt.Sprint(MockScmScanResults(MockScmModules, MockScmNamespaces, MockServers)),
			"map[1.2.3.4:10000:SCM Namespaces: pmem1/2.90TB/numa1\n 1.2.3.5:10001:SCM Namespaces: pmem1/2.90TB/numa1\n]",
		},
		"scm scan without pmem namespaces": {
			fmt.Sprint(MockScmScanResults(MockScmModules, []*ctlpb.PmemDevice{}, MockServers)),
			"map[1.2.3.4:10000:SCM Modules:\n\tPhysicalID:12345 Capacity:12.06KB Location:(socket:4 memctrlr:3 chan:1 pos:2)\n\n 1.2.3.5:10001:SCM Modules:\n\tPhysicalID:12345 Capacity:12.06KB Location:(socket:4 memctrlr:3 chan:1 pos:2)\n\n]",
		},
		"scm mount scan": { // currently unused
			NewClientScmMount(MockMounts, MockServers).String(),
			"1.2.3.4:10000:\n\tmntpoint:\"/mnt/daos\" \n\n1.2.3.5:10001:\n\tmntpoint:\"/mnt/daos\" \n\n",
		},
		"generic cmd results": {
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, MockErr}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, MockErr}}.String(),
			"1.2.3.4:10000:\n\terror: unknown failure\n1.2.3.5:10001:\n\terror: unknown failure\n",
		},
		"nvme operation results": {
			NewClientNvmeResults(
				[]*ctlpb.NvmeControllerResult{
					{
						Pciaddr: "0000:81:00.0",
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_ERR_APP,
							Error:  "example application error",
						},
					},
				}, MockServers).String(),
			"1.2.3.4:10000:\n\tPCI Addr:0000:81:00.0 Status:CTL_ERR_APP Error:example application error\n\n1.2.3.5:10001:\n\tPCI Addr:0000:81:00.0 Status:CTL_ERR_APP Error:example application error\n\n",
		},
		//"scm operation results": { // currently unused
		//		{
		//			NewClientScmResults(
		//				[]*ctlpb.ScmModuleResult{
		//					{
		//						Loc: MockModulePB().Loc,
		//						State: &ctlpb.ResponseState{
		//							Status: ctlpb.ResponseStatus_CTL_ERR_APP,
		//							Error:  "example application error",
		//						},
		//					},
		//				}, MockServers).String(),
		//			"1.2.3.4:10000:\n\tModule Location:(socket:4 memctrlr:3 chan:1 pos:2) Status:CTL_ERR_APP Error:example application error\n\n1.2.3.5:10001:\n\tModule Location:(socket:4 memctrlr:3 chan:1 pos:2) Status:CTL_ERR_APP Error:example application error\n\n",
		//		},
		"scm mountpoint operation results": {
			NewClientScmMountResults(
				[]*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_ERR_APP,
							Error:  "example application error",
						},
					},
				}, MockServers).String(),
			"1.2.3.4:10000:\n\tMntpoint:/mnt/daos Status:CTL_ERR_APP Error:example application error\n\n1.2.3.5:10001:\n\tMntpoint:/mnt/daos Status:CTL_ERR_APP Error:example application error\n\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tt.out, tt.m); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
