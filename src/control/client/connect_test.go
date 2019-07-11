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

	"github.com/pkg/errors"
	. "google.golang.org/grpc/connectivity"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func connectSetup(
	state State, features []*pb.Feature, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, mountResults ScmMountResults,
	scanRet error, formatRet error, updateRet error, burninRet error,
	killRet error, connectRet error) Connect {

	connect := newMockConnect(
		state, features, ctrlrs, ctrlrResults, modules,
		moduleResults, mountResults, scanRet, formatRet,
		updateRet, burninRet, killRet, connectRet)

	_ = connect.ConnectClients(MockServers)

	return connect
}

func defaultClientSetup() Connect {
	cc := defaultMockConnect()

	_ = cc.ConnectClients(MockServers)

	return cc
}

func checkResults(t *testing.T, addrs Addresses, results ResultMap, e error) {
	AssertEqual(
		t, len(results), len(addrs), // duplicates ignored
		"unexpected number of results")

	for _, res := range results {
		AssertEqual(
			t, res.Err, e,
			"unexpected error value in results")
	}
}

func TestConnectClients(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

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
			tt.state, MockFeatures, MockCtrlrs, MockCtrlrResults, MockModules,
			MockModuleResults, MockMountResults, nil, nil, nil, nil, nil,
			tt.connRet)

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
	defer common.ShowLogOnFailure(t)()

	cc := defaultMockConnect()
	results := cc.ConnectClients(append(MockServers, MockServers...))

	checkResults(t, MockServers, results, nil)
}

func TestGetClearConns(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	cc := defaultClientSetup()

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

func TestListFeatures(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	cc := defaultClientSetup()

	clientFeatures := cc.ListFeatures()

	AssertEqual(
		t, clientFeatures, NewClientFM(MockFeatures, MockServers),
		"unexpected client features returned")
}

func TestScanStorage(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	cc := defaultClientSetup()

	clientNvme, clientScm := cc.ScanStorage()

	AssertEqual(
		t, clientNvme, NewClientNvme(MockCtrlrs, MockServers),
		"unexpected client NVMe SSD controllers returned")

	AssertEqual(
		t, clientScm, NewClientScm(MockModules, MockServers),
		"unexpected client SCM modules returned")
}

func TestFormatStorage(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	tests := []struct {
		formatRet error
	}{
		{
			nil,
		},
		{
			MockErr,
		},
	}

	for _, tt := range tests {
		cc := connectSetup(
			Ready, MockFeatures, MockCtrlrs, MockCtrlrResults, MockModules,
			MockModuleResults, MockMountResults, nil, tt.formatRet, nil, nil,
			nil, nil)

		cNvmeMap, cMountMap := cc.FormatStorage()

		if tt.formatRet != nil {
			for _, addr := range MockServers {
				AssertEqual(
					t, cNvmeMap[addr],
					CtrlrResults{Err: tt.formatRet},
					"unexpected error for nvme result")
				AssertEqual(
					t, cMountMap[addr],
					MountResults{Err: tt.formatRet},
					"unexpected error for scm mount result")
			}
			continue
		}

		AssertEqual(
			t, cNvmeMap, NewClientNvmeResults(MockCtrlrResults, MockServers),
			"unexpected client NVMe SSD controller results returned")

		AssertEqual(
			t, cMountMap, NewClientScmMountResults(MockMountResults, MockServers),
			"unexpected client SCM Mount results returned")
	}
}

func TestUpdateStorage(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	tests := []struct {
		updateRet error
	}{
		{
			nil,
		},
		{
			errors.New(msgOpenStreamFail + MockErr.Error()),
		},
	}

	for _, tt := range tests {
		cc := connectSetup(
			Ready, MockFeatures, MockCtrlrs, MockCtrlrResults, MockModules,
			MockModuleResults, MockMountResults, nil, nil, tt.updateRet, nil,
			nil, nil)

		cNvmeMap, cModuleMap := cc.UpdateStorage(new(pb.UpdateStorageReq))

		if tt.updateRet != nil {
			for _, addr := range MockServers {
				AssertEqual(
					t, cNvmeMap[addr],
					CtrlrResults{Err: tt.updateRet},
					"unexpected error for nvme result")
				AssertEqual(
					t, cModuleMap[addr],
					ModuleResults{Err: tt.updateRet},
					"unexpected error for scm module result")
			}
			continue
		}

		AssertEqual(
			t, cNvmeMap, NewClientNvmeResults(MockCtrlrResults, MockServers),
			"unexpected client NVMe SSD controller results returned")

		AssertEqual(
			t, cModuleMap, NewClientScmResults(MockModuleResults, MockServers),
			"unexpected client SCM Module results returned")
	}
}

func TestKillRank(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	tests := []struct {
		killRet error
	}{
		{
			nil,
		},
		{
			MockErr,
		},
	}

	for _, tt := range tests {
		cc := connectSetup(
			Ready, MockFeatures, MockCtrlrs, MockCtrlrResults, MockModules,
			MockModuleResults, MockMountResults, nil, nil, nil, nil,
			tt.killRet, nil)

		resultMap := cc.KillRank("acd", 0)

		checkResults(t, Addresses{MockServers[0]}, resultMap, tt.killRet)
	}
}
