//
// (C) Copyright 2018 Intel Corporation.
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
	"fmt"
	"testing"

	. "github.com/daos-stack/daos/src/control/client"
	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

var (
	addresses = Addresses{"1.2.3.4:10000", "1.2.3.5:10001"}
	features  = []*pb.Feature{MockFeaturePB()}
	ctrlrs    = NvmeControllers{MockControllerPB("")}
	modules   = ScmModules{MockModulePB()}
)

func TestHasConnection(t *testing.T) {
	var shelltests = []struct {
		addrs Addresses
		eMap  ErrorMap
		out   string
	}{
		{
			addresses,
			ErrorMap{},
			"Active connections: [1.2.3.4:10000 1.2.3.5:10001]\n",
		},
		{
			Addresses{"1.2.3.5:10001"},
			ErrorMap{"1.2.3.4:10000": errors.New("test")},
			"failed to connect to 1.2.3.4:10000 (test)\nActive connections: [1.2.3.5:10001]\n",
		},
		{
			Addresses{},
			ErrorMap{"1.2.3.4:10000": errors.New("test"), "1.2.3.5:10001": errors.New("test")},
			"failed to connect to 1.2.3.4:10000 (test)\nfailed to connect to 1.2.3.5:10001 (test)\nActive connections: []\nNo active connections!",
		},
	}
	for _, tt := range shelltests {
		AssertEqual(t, hasConnections(tt.addrs, tt.eMap), tt.out, "bad output")
	}
}

func TestSprintConns(t *testing.T) {
	var shelltests = []struct {
		addrs Addresses
		eMap  ErrorMap
		out   string
	}{
		{
			addresses,
			ErrorMap{},
			"Active connections: [1.2.3.4:10000 1.2.3.5:10001]\n",
		},
		{
			Addresses{"1.2.3.5:10001"},
			ErrorMap{"1.2.3.4:10000": errors.New("test")},
			"failed to connect to 1.2.3.4:10000 (test)\nActive connections: [1.2.3.5:10001]\n",
		},
		{
			Addresses{},
			ErrorMap{"1.2.3.4:10000": errors.New("test"), "1.2.3.5:10001": errors.New("test")},
			"failed to connect to 1.2.3.4:10000 (test)\nfailed to connect to 1.2.3.5:10001 (test)\nActive connections: []\n",
		},
	}
	for _, tt := range shelltests {
		AssertEqual(t, sprintConns(tt.addrs, tt.eMap), tt.out, "bad output")
	}
}

func marshal(i interface{}) string {
	s, _ := StructsToString(i)
	return s
}

func TestCheckSprint(t *testing.T) {
	var shelltests = []struct {
		m   interface{}
		err error
		out string
	}{
		{
			NewClientFM(features, addresses),
			errors.New("test"),
			"Unable to retrieve %[1]ss (test)\n",
		},
		{
			NewClientFM(features, addresses),
			nil,
			fmt.Sprintf(
				"Listing %%[1]ss on connected storage servers:\n%s\n",
				marshal(NewClientFM(features, addresses))),
		},
		{
			NewClientNvme(ctrlrs, addresses),
			nil,
			fmt.Sprintf(
				"Listing %%[1]ss on connected storage servers:\n%s\n",
				marshal(NewClientNvme(ctrlrs, addresses))),
		},
		{
			NewClientScm(modules, addresses),
			nil,
			fmt.Sprintf(
				"Listing %%[1]ss on connected storage servers:\n%s\n",
				marshal(NewClientScm(modules, addresses))),
		},
	}
	for _, tt := range shelltests {
		AssertEqual(t, checkAndFormat(tt.m, tt.err), tt.out, "bad output")
	}
}
