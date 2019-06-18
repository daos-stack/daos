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
	"errors"
	"os"
	"testing"

	. "github.com/daos-stack/daos/src/control/client"
	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
)

var (
	addresses  = Addresses{"1.2.3.4:10000", "1.2.3.5:10001"}
	features   = []*pb.Feature{MockFeaturePB()}
	ctrlrs     = NvmeControllers{MockControllerPB("")}
	modules    = ScmModules{MockModulePB()}
	errExample = errors.New("something went wrong")
)

func init() {
	log.NewDefaultLogger(log.Error, "dmg_tests: ", os.Stderr)
}

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
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, errExample}},
			"failed to connect to 1.2.3.4:10000 (something went wrong)\nActive connections: []\nNo active connections!",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, nil}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, nil}},
			"Active connections: [1.2.3.4:10000 1.2.3.5:10001]\n",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, errExample}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, errExample}},
			"failed to connect to 1.2.3.4:10000 (something went wrong)\nfailed to connect to 1.2.3.5:10001 (something went wrong)\nActive connections: []\nNo active connections!",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, errExample}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, nil}},
			"failed to connect to 1.2.3.4:10000 (something went wrong)\nActive connections: [1.2.3.5:10001]\n",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, nil}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, errExample}},
			"failed to connect to 1.2.3.5:10001 (something went wrong)\nActive connections: [1.2.3.4:10000]\n",
		},
	}

	for _, tt := range shelltests {
		_, out := hasConns(tt.results)
		AssertEqual(t, out, tt.out, "bad output")
	}
}

func marshal(i interface{}) string {
	s, _ := StructsToString(i)
	return s
}

func TestCheckSprint(t *testing.T) {
	var shelltests = []struct {
		m   string
		out string
	}{
		{
			NewClientFM(features, addresses).String(),
			"1.2.3.4:10000:\nburn-name: category nvme, run workloads on device to test\n\n1.2.3.5:10001:\nburn-name: category nvme, run workloads on device to test\n\n",
		},
		{
			NewClientNvme(ctrlrs, addresses).String(),
			"1.2.3.4:10000:\n\tPCI Address:0000:81:00.0 Serial:123ABC Model:ABC\n\t\tNamespace: id:12345 capacity:99999 \n\n1.2.3.5:10001:\n\tPCI Address:0000:81:00.0 Serial:123ABC Model:ABC\n\t\tNamespace: id:12345 capacity:99999 \n\n",
		},
		{
			NewClientScm(modules, addresses).String(),
			"1.2.3.4:10000:\n\tphysicalid:12345 capacity:12345 loc:<channel:1 channelpos:2 memctrlr:3 socket:4 > \n\n1.2.3.5:10001:\n\tphysicalid:12345 capacity:12345 loc:<channel:1 channelpos:2 memctrlr:3 socket:4 > \n\n",
		},
		{
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, errExample}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, errExample}}.String(),
			"1.2.3.4:10000:\nerror: something went wrong\n1.2.3.5:10001:\nerror: something went wrong\n",
		},
		{
			NewClientMountResults(
				[]*pb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &pb.ResponseState{
							Status: pb.ResponseStatus_CTRL_ERR_APP,
							Error:  "example application error",
						},
					},
				}, addresses).String(),
			"1.2.3.4:10000:\n\tmntpoint /mnt/daos: status CTRL_ERR_APP error: example application error\n\n1.2.3.5:10001:\n\tmntpoint /mnt/daos: status CTRL_ERR_APP error: example application error\n\n",
		},
		// TODO: add test cases for feature/mount, ctrlr/module results
	}
	for _, tt := range shelltests {
		AssertEqual(t, tt.m, tt.out, "bad output")
	}
}
