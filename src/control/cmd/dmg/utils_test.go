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
	"testing"

	. "github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func TestHasConnection(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

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
		AssertEqual(t, out, tt.out, "bad output")
	}
}

func TestCheckSprint(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	var shelltests = []struct {
		m   string
		out string
	}{
		{
			NewClientFM(MockFeatures, MockServers).String(),
			"1.2.3.4:10000:\nburn-name: category nvme, run workloads on device to test\n\n1.2.3.5:10001:\nburn-name: category nvme, run workloads on device to test\n\n",
		},
		{
			NewClientNvme(MockCtrlrs, MockServers).String(),
			"1.2.3.4:10000:\n\tPCI Address:0000:81:00.0 Serial:123ABC Model:ABC\n\t\tNamespace: id:12345 capacity:99999 \n\n1.2.3.5:10001:\n\tPCI Address:0000:81:00.0 Serial:123ABC Model:ABC\n\t\tNamespace: id:12345 capacity:99999 \n\n",
		},
		{
			NewClientScm(MockModules, MockServers).String(),
			"1.2.3.4:10000:\n\tphysicalid:12345 capacity:12345 loc:<channel:1 channelpos:2 memctrlr:3 socket:4 > \n\n1.2.3.5:10001:\n\tphysicalid:12345 capacity:12345 loc:<channel:1 channelpos:2 memctrlr:3 socket:4 > \n\n",
		},
		{
			NewClientScmMount(MockMounts, MockServers).String(),
			"1.2.3.4:10000:\n\tmntpoint:\"/mnt/daos\" \n\n1.2.3.5:10001:\n\tmntpoint:\"/mnt/daos\" \n\n",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, MockErr}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, MockErr}}.String(),
			"1.2.3.4:10000:\nerror: unknown failure\n1.2.3.5:10001:\nerror: unknown failure\n",
		},
		{
			NewClientNvmeResults(
				[]*pb.NvmeControllerResult{
					{
						Pciaddr: "0000:81:00.0",
						State: &pb.ResponseState{
							Status: pb.ResponseStatus_CTRL_ERR_APP,
							Error:  "example application error",
						},
					},
				}, MockServers).String(),
			"1.2.3.4:10000:\n\tpci-address 0000:81:00.0: status CTRL_ERR_APP error: example application error\n\n1.2.3.5:10001:\n\tpci-address 0000:81:00.0: status CTRL_ERR_APP error: example application error\n\n",
		},
		{
			NewClientScmResults(
				[]*pb.ScmModuleResult{
					{
						Loc: MockModulePB().Loc,
						State: &pb.ResponseState{
							Status: pb.ResponseStatus_CTRL_ERR_APP,
							Error:  "example application error",
						},
					},
				}, MockServers).String(),
			"1.2.3.4:10000:\n\tmodule location channel:1 channelpos:2 memctrlr:3 socket:4 : status CTRL_ERR_APP error: example application error\n\n1.2.3.5:10001:\n\tmodule location channel:1 channelpos:2 memctrlr:3 socket:4 : status CTRL_ERR_APP error: example application error\n\n",
		},
		{
			NewClientScmMountResults(
				[]*pb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &pb.ResponseState{
							Status: pb.ResponseStatus_CTRL_ERR_APP,
							Error:  "example application error",
						},
					},
				}, MockServers).String(),
			"1.2.3.4:10000:\n\tmntpoint /mnt/daos: status CTRL_ERR_APP error: example application error\n\n1.2.3.5:10001:\n\tmntpoint /mnt/daos: status CTRL_ERR_APP error: example application error\n\n",
		},
	}
	for _, tt := range shelltests {
		AssertEqual(t, tt.m, tt.out, "bad output")
	}
}
