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

package client

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	. "google.golang.org/grpc/connectivity"

	. "github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	. "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/logging"
)

func connectSetupServers(
	servers Addresses, log logging.Logger,
	state State, features []*ctlpb.Feature, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, pmems PmemDevices, mountResults ScmMountResults,
	scanRet error, formatRet error, updateRet error, burninRet error,
	killRet error, connectRet error, getACLRet *mockGetACLResult) Connect {

	connect := newMockConnect(
		log, state, features, ctrlrs, ctrlrResults, modules,
		moduleResults, pmems, mountResults, scanRet, formatRet,
		updateRet, burninRet, killRet, connectRet, getACLRet)

	_ = connect.ConnectClients(servers)

	return connect
}

func connectSetup(
	log logging.Logger,
	state State, features []*ctlpb.Feature, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, pmems PmemDevices, mountResults ScmMountResults,
	scanRet error, formatRet error, updateRet error, burninRet error,
	killRet error, connectRet error, getACLRet *mockGetACLResult) Connect {

	return connectSetupServers(MockServers, log, state, features, ctrlrs,
		ctrlrResults, modules, moduleResults, pmems, mountResults, scanRet,
		formatRet, updateRet, burninRet, killRet, connectRet, getACLRet)
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
			log, tt.state, MockFeatures, MockCtrlrs, MockCtrlrResults, MockModules,
			MockModuleResults, MockPmemDevices, MockMountResults,
			nil, nil, nil, nil, nil, tt.connRet, nil)

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

func TestListFeatures(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	cc := defaultClientSetup(log)

	clientFeatures := cc.ListFeatures()

	AssertEqual(
		t, clientFeatures, NewClientFM(MockFeatures, MockServers),
		"unexpected client features returned")
}

func TestStorageScan(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	cc := defaultClientSetup(log)

	clientNvme, clientScm, clientPmem := cc.StorageScan()

	AssertEqual(t, clientNvme, NewClientNvme(MockCtrlrs, MockServers),
		"unexpected client NVMe SSD controllers returned")

	AssertEqual(t, clientScm, NewClientScm(MockModules, MockServers),
		"unexpected client SCM modules returned")

	AssertEqual(t, clientPmem, NewClientPmem(MockPmemDevices, MockServers),
		"unexpected client PMEM device files returned")
}

func TestStorageFormat(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	for name, tt := range map[string]struct {
		formatRet error
		reformat  bool
	}{
		"ok": {},
		"fails": {
			formatRet: MockErr,
		},
	} {
		t.Run(name, func(t *testing.T) {
			cc := connectSetup(
				log, Ready, MockFeatures, MockCtrlrs, MockCtrlrResults, MockModules,
				MockModuleResults, MockPmemDevices, MockMountResults, nil, tt.formatRet, nil, nil,
				nil, nil, MockACL)

			cNvmeMap, cMountMap := cc.StorageFormat(tt.reformat)

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
				return
			}

			AssertEqual(
				t, cNvmeMap, NewClientNvmeResults(MockCtrlrResults, MockServers),
				"unexpected client NVMe SSD controller results returned")

			AssertEqual(
				t, cMountMap, NewClientScmMountResults(MockMountResults, MockServers),
				"unexpected client SCM Mount results returned")
		})
	}
}

func TestStorageUpdate(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

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
			log, Ready, MockFeatures, MockCtrlrs, MockCtrlrResults, MockModules,
			MockModuleResults, MockPmemDevices, MockMountResults, nil,
			nil, tt.updateRet, nil, nil, nil, MockACL)

		cNvmeMap, cModuleMap := cc.StorageUpdate(new(ctlpb.StorageUpdateReq))

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
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	tests := []struct {
		killRet error
	}{
		{
			nil,
		},
	}

	for _, tt := range tests {
		cc := connectSetup(log, Ready, MockFeatures, MockCtrlrs, MockCtrlrResults, MockModules,
			MockModuleResults, MockPmemDevices, MockMountResults, nil, nil, nil,
			nil, tt.killRet, nil, MockACL)

		resultMap := cc.KillRank(0)

		checkResults(t, Addresses{MockServers[0]}, resultMap, tt.killRet)
	}
}

func TestPoolGetACL(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	for name, tt := range map[string]struct {
		addr             Addresses
		getACLRespStatus int32
		getACLErr        error
		expectedResp     *PoolGetACLResp
		expectedErr      string
	}{
		"no service leader": {
			addr:         nil,
			expectedResp: nil,
			expectedErr:  "no active connections",
		},
		"gRPC call failed": {
			addr:         MockServers,
			getACLErr:    MockErr,
			expectedResp: nil,
			expectedErr:  MockErr.Error(),
		},
		"gRPC resp bad status": {
			addr:             MockServers,
			getACLRespStatus: -5000,
			expectedResp:     nil,
			expectedErr:      "DAOS returned error code: -5000",
		},
		"success": {
			addr:             MockServers,
			getACLRespStatus: 0,
			expectedResp:     &PoolGetACLResp{ACL: MockACL.acl},
			expectedErr:      "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var expectedACL []string
			if tt.expectedResp != nil {
				expectedACL = tt.expectedResp.ACL
			}
			aclResult := &mockGetACLResult{
				acl:    expectedACL,
				status: tt.getACLRespStatus,
				err:    tt.getACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready, MockFeatures,
				MockCtrlrs, MockCtrlrResults, MockModules,
				MockModuleResults, MockPmemDevices, MockMountResults,
				nil, nil, nil, nil, nil, nil,
				aclResult)

			req := &PoolGetACLReq{
				UUID: "TestUUID",
			}

			resp, err := cc.PoolGetACL(req)

			if tt.expectedErr != "" {
				ExpectError(t, err, tt.expectedErr, name)
			} else if err != nil {
				t.Fatalf("expected nil error, got %v", err)
			}

			if diff := cmp.Diff(tt.expectedResp, resp); diff != "" {
				t.Fatalf("unexpected ACL (-want, +got):\n%s\n", diff)
			}
		})
	}
}
