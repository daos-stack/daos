//
// (C) Copyright 2018-2020 Intel Corporation.
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

	. "google.golang.org/grpc/connectivity"

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/logging"
)

func connectSetupServers(
	servers Addresses, log logging.Logger, state State, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, pmems ScmNamespaces, mountResults ScmMountResults,
	scanRet error, formatRet error, killRet error, connectRet error,
	listPoolsRet *MockListPoolsResult) Connect {

	connect := newMockConnect(
		log, state, ctrlrs, ctrlrResults, modules,
		moduleResults, pmems, mountResults, scanRet, formatRet,
		killRet, connectRet, listPoolsRet)

	_ = connect.ConnectClients(servers)

	return connect
}

func connectSetup(
	log logging.Logger,
	state State, ctrlrs NvmeControllers, ctrlrResults NvmeControllerResults,
	modules ScmModules, moduleResults ScmModuleResults, pmems ScmNamespaces,
	mountResults ScmMountResults, scanRet error, formatRet error,
	killRet error, connectRet error,
	listPoolsRet *MockListPoolsResult) Connect {

	return connectSetupServers(MockServers, log, state, ctrlrs,
		ctrlrResults, modules, moduleResults, pmems, mountResults, scanRet,
		formatRet, killRet, connectRet, listPoolsRet)
}

func defaultClientSetup(log logging.Logger) Connect {
	cc := defaultMockConnect(log)

	_ = cc.ConnectClients(MockServers)

	return cc
}

func checkResults(t *testing.T, addrs Addresses, results ResultMap, e error) {
	t.Helper()

	// duplicates ignored
	AssertEqual(t, len(results), len(addrs), "unexpected number of results")

	for _, res := range results {
		AssertEqual(t, res.Err, e, "unexpected error value in results")
	}
}

func TestConnectClients(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	eMsg := "socket connection is not active (%s)"

	var conntests = []struct {
		addrsIn Addresses
		state   State
		connRet error
		errMsg  string
	}{
		{MockServers, Idle, nil, ""},
		{MockServers, Connecting, nil, fmt.Sprintf(eMsg, Connecting)},
		{MockServers, Ready, nil, ""},
		{MockServers, TransientFailure, nil, fmt.Sprintf(eMsg, TransientFailure)},
		{MockServers, Shutdown, nil, fmt.Sprintf(eMsg, Shutdown)},
		{MockServers, Idle, MockErr, "unknown failure"},
		{MockServers, Connecting, MockErr, "unknown failure"},
		{MockServers, Ready, MockErr, "unknown failure"},
	}
	for _, tt := range conntests {
		cc := newMockConnect(
			log, tt.state, MockCtrlrs, MockCtrlrResults, MockScmModules,
			MockModuleResults, MockScmNamespaces, MockMountResults,
			nil, nil, nil, tt.connRet, nil)

		results := cc.ConnectClients(tt.addrsIn)

		AssertEqual(
			t, len(results), len(tt.addrsIn), // assumes no duplicates
			"unexpected number of results")

		for _, res := range results {
			if tt.errMsg == "" {
				AssertEqual(
					t, res.Err, nil,
					"unexpected non-nil error value in results")
				continue
			}

			AssertEqual(
				t, res.Err.Error(), tt.errMsg,
				"unexpected error value in results")
		}
	}
}

func TestDuplicateConns(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	cc := defaultMockConnect(log)
	results := cc.ConnectClients(append(MockServers, MockServers...))

	checkResults(t, MockServers, results, nil)
}

func TestGetClearConns(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	cc := defaultClientSetup(log)

	results := cc.GetActiveConns(ResultMap{})
	checkResults(t, MockServers, results, nil)

	results = cc.ClearConns()
	checkResults(t, MockServers, results, nil)

	results = cc.GetActiveConns(ResultMap{})
	AssertEqual(t, results, ResultMap{}, "unexpected result map")

	results = cc.ConnectClients(MockServers)
	checkResults(t, MockServers, results, nil)

	results = cc.GetActiveConns(results)
	checkResults(t, MockServers, results, nil)
}
