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

package client

import (
	"fmt"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"

	"google.golang.org/grpc/connectivity"
)

var (
	addresses = Addresses{"1.2.3.4:10000", "1.2.3.5:10001"}
	features  = []*pb.Feature{MockFeaturePB()}
	ctrlrs    = NvmeControllers{MockControllerPB("")}
	modules   = ScmModules{MockModulePB()}
)

type mockConnFactory struct {
	state    connectivity.State
	features []*pb.Feature
	ctrlrs   NvmeControllers
	modules  ScmModules
}

func (m *mockConnFactory) createConn(address string) Control {
	return newMockControl(address, m.state, m.features, m.ctrlrs, m.modules)
}

func newMockConnections(
	state connectivity.State, features []*pb.Feature, ctrlrs NvmeControllers,
	modules ScmModules) Connections {

	return &connList{factory: &mockConnFactory{state, features, ctrlrs, modules}}
}

func TestConnectClients(t *testing.T) {
	var conntests = []struct {
		addrsIn       Addresses
		addrsOut      Addresses
		state         connectivity.State
		shouldSucceed bool
	}{
		{addresses, addresses, connectivity.Idle, true},
		{addresses, Addresses{}, connectivity.Connecting, false},
		{addresses, addresses, connectivity.Ready, true},
		{addresses, Addresses{}, connectivity.TransientFailure, false},
		{addresses, Addresses{}, connectivity.Shutdown, false},
	}
	for _, tt := range conntests {
		cc := newMockConnections(tt.state, features, ctrlrs, modules)

		addrs, eMap := cc.ConnectClients(tt.addrsIn)

		if tt.shouldSucceed {
			AssertEqual(t, addrs, tt.addrsOut, "unexpected client address list returned")
			AssertEqual(t, eMap, ErrorMap{}, "unexpected non-nil failure map")
			continue
		}
		AssertEqual(t, len(addrs), 0, "unexpected number of clients connected")
		AssertEqual(t, len(eMap), len(tt.addrsIn), "unexpected failure map")
		for _, addr := range tt.addrsIn {
			AssertEqual(
				t, eMap[addr],
				fmt.Errorf("socket connection is not active (%s)", tt.state),
				"unexpected failure in map")
		}
	}
}

func clientSetup(
	t *testing.T, state connectivity.State, features []*pb.Feature,
	ctrlrs NvmeControllers, modules ScmModules) Connections {

	cc := newMockConnections(state, features, ctrlrs, modules)
	_, _ = cc.ConnectClients(addresses)
	return cc
}

func TestDuplicateConns(t *testing.T) {
	cc := newMockConnections(connectivity.Ready, features, ctrlrs, modules)
	addrs, eMap := cc.ConnectClients(append(addresses, addresses...))

	// verify duplicates are removed and failed attempts recorded.
	AssertEqual(t, addrs, addresses, "unexpected client address list returned")
	AssertEqual(t, len(eMap), len(addresses), "unexpected failure map")
	for _, addr := range addresses {
		AssertEqual(
			t, eMap[addr],
			fmt.Errorf("duplicate connection to %s", addr),
			"unexpected failure in map")
	}
}

func TestGetClearConns(t *testing.T) {
	cc := clientSetup(t, connectivity.Ready, features, ctrlrs, modules)

	addrs, eMap := cc.GetActiveConns(ErrorMap{})
	AssertEqual(t, addrs, addresses, "unexpected client address list returned")
	AssertEqual(t, eMap, ErrorMap{}, "unexpected failure map")

	eMap = cc.ClearConns()
	AssertEqual(t, eMap, ErrorMap{}, "unexpected failure map")

	addrs, eMap = cc.GetActiveConns(ErrorMap{})
	AssertEqual(t, addrs, Addresses(nil), "expected nil client address list to be returned")
	AssertEqual(t, eMap, ErrorMap{}, "unexpected failure map")

	addrs, eMap = cc.ConnectClients(addresses)
	AssertEqual(t, addrs, addresses, "unexpected client address list returned")
	AssertEqual(t, eMap, ErrorMap{}, "unexpected failure map")

	addrs, eMap = cc.GetActiveConns(ErrorMap{})
	AssertEqual(t, addrs, addresses, "unexpected client address list returned")
	AssertEqual(t, eMap, ErrorMap{}, "unexpected failure map")
}

func TestListFeatures(t *testing.T) {
	cc := clientSetup(t, connectivity.Ready, features, ctrlrs, modules)

	clientFeatures, err := cc.ListFeatures()
	if err != nil {
		t.Fatal(err)
	}

	AssertEqual(
		t, clientFeatures, NewClientFM(features, addresses),
		"unexpected client features returned")
}

func TestListNvme(t *testing.T) {
	cc := clientSetup(t, connectivity.Ready, features, ctrlrs, modules)

	clientNvme, err := cc.ListNvme()
	if err != nil {
		t.Fatal(err)
	}

	AssertEqual(
		t, clientNvme, NewClientNvme(ctrlrs, addresses),
		"unexpected client NVMe SSD controllers returned")
}

func TestListScm(t *testing.T) {
	cc := clientSetup(t, connectivity.Ready, features, ctrlrs, modules)

	clientScm, err := cc.ListScm()
	if err != nil {
		t.Fatal(err)
	}

	AssertEqual(
		t, clientScm, NewClientScm(modules, addresses),
		"unexpected client SCM modules returned")
}
