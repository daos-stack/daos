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
	. "google.golang.org/grpc/connectivity"

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/logging"
)

func connectSetupServers(
	servers Addresses, log logging.Logger, state State, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, pmems ScmNamespaces, mountResults ScmMountResults,
	scanRet error, formatRet error, killRet error, connectRet error,
	ACLRet *mockACLResult) Connect {

	connect := newMockConnect(
		log, state, ctrlrs, ctrlrResults, modules,
		moduleResults, pmems, mountResults, scanRet, formatRet,
		killRet, connectRet, ACLRet)

	_ = connect.ConnectClients(servers)

	return connect
}

func connectSetup(
	log logging.Logger,
	state State, ctrlrs NvmeControllers, ctrlrResults NvmeControllerResults,
	modules ScmModules, moduleResults ScmModuleResults, pmems ScmNamespaces,
	mountResults ScmMountResults, scanRet error, formatRet error,
	killRet error, connectRet error, ACLRet *mockACLResult) Connect {

	return connectSetupServers(MockServers, log, state, ctrlrs,
		ctrlrResults, modules, moduleResults, pmems, mountResults, scanRet,
		formatRet, killRet, connectRet, ACLRet)
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

func TestStorageScan(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	cc := defaultClientSetup(log)

	clientResp := cc.StorageScan(&StorageScanReq{})

	AssertEqual(t, MockScanResp(MockCtrlrs, MockScmModules, MockScmNamespaces, MockServers, false), clientResp, "")
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
				log, Ready, MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, tt.formatRet, nil, nil, MockACL)

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
		cc := connectSetup(log, Ready, MockCtrlrs, MockCtrlrResults, MockScmModules,
			MockModuleResults, MockScmNamespaces, MockMountResults, nil,
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
			expectedResp:     &PoolGetACLResp{ACL: MockACL.ACL()},
			expectedErr:      "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var expectedACL []string
			if tt.expectedResp != nil {
				expectedACL = tt.expectedResp.ACL.Entries
			}
			aclResult := &mockACLResult{
				acl:    expectedACL,
				status: tt.getACLRespStatus,
				err:    tt.getACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, aclResult)

			req := PoolGetACLReq{
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

func TestPoolOverwriteACL(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	testACL := &AccessControlList{
		Entries: MockACL.acl,
	}

	for name, tt := range map[string]struct {
		addr                   Addresses
		inputACL               *AccessControlList
		overwriteACLRespStatus int32
		overwriteACLErr        error
		expectedResp           *PoolOverwriteACLResp
		expectedErr            string
	}{
		"no service leader": {
			addr:         nil,
			inputACL:     testACL,
			expectedResp: nil,
			expectedErr:  "no active connections",
		},
		"gRPC call failed": {
			addr:            MockServers,
			inputACL:        testACL,
			overwriteACLErr: MockErr,
			expectedResp:    nil,
			expectedErr:     MockErr.Error(),
		},
		"gRPC resp bad status": {
			addr:                   MockServers,
			inputACL:               testACL,
			overwriteACLRespStatus: -5001,
			expectedResp:           nil,
			expectedErr:            "DAOS returned error code: -5001",
		},
		"success": {
			addr:                   MockServers,
			inputACL:               testACL,
			overwriteACLRespStatus: 0,
			expectedResp:           &PoolOverwriteACLResp{ACL: testACL},
			expectedErr:            "",
		},
		"nil input": {
			addr:                   MockServers,
			inputACL:               nil,
			overwriteACLRespStatus: 0,
			expectedResp:           &PoolOverwriteACLResp{ACL: &AccessControlList{}},
			expectedErr:            "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var expectedACL []string
			if tt.expectedResp != nil {
				expectedACL = tt.expectedResp.ACL.Entries
			}
			aclResult := &mockACLResult{
				acl:    expectedACL,
				status: tt.overwriteACLRespStatus,
				err:    tt.overwriteACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, aclResult)

			req := PoolOverwriteACLReq{
				UUID: "TestUUID",
				ACL:  tt.inputACL,
			}

			resp, err := cc.PoolOverwriteACL(req)

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

func TestPoolUpdateACL(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	testACL := &AccessControlList{
		Entries: MockACL.acl,
	}

	for name, tt := range map[string]struct {
		addr                Addresses
		inputACL            *AccessControlList
		updateACLRespStatus int32
		updateACLErr        error
		expectedResp        *PoolUpdateACLResp
		expectedErr         string
	}{
		"no service leader": {
			addr:         nil,
			inputACL:     testACL,
			expectedResp: nil,
			expectedErr:  "no active connections",
		},
		"gRPC call failed": {
			addr:         MockServers,
			inputACL:     testACL,
			updateACLErr: MockErr,
			expectedResp: nil,
			expectedErr:  MockErr.Error(),
		},
		"gRPC resp bad status": {
			addr:                MockServers,
			inputACL:            testACL,
			updateACLRespStatus: -5001,
			expectedResp:        nil,
			expectedErr:         "DAOS returned error code: -5001",
		},
		"success": {
			addr:                MockServers,
			inputACL:            testACL,
			updateACLRespStatus: 0,
			expectedResp:        &PoolUpdateACLResp{ACL: testACL},
			expectedErr:         "",
		},
		"nil input": {
			addr:         MockServers,
			inputACL:     nil,
			expectedResp: nil,
			expectedErr:  "no entries requested",
		},
		"empty ACL input": {
			addr:         MockServers,
			inputACL:     &AccessControlList{},
			expectedResp: nil,
			expectedErr:  "no entries requested",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var expectedACL []string
			if tt.expectedResp != nil {
				expectedACL = tt.expectedResp.ACL.Entries
			}
			aclResult := &mockACLResult{
				acl:    expectedACL,
				status: tt.updateACLRespStatus,
				err:    tt.updateACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, aclResult)

			req := PoolUpdateACLReq{
				UUID: "TestUUID",
				ACL:  tt.inputACL,
			}

			resp, err := cc.PoolUpdateACL(req)

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

func TestPoolDeleteACL(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	for name, tt := range map[string]struct {
		addr                Addresses
		inputPrincipal      string
		deleteACLRespStatus int32
		deleteACLErr        error
		expectedResp        *PoolDeleteACLResp
		expectedErr         string
	}{
		"no service leader": {
			addr:           nil,
			inputPrincipal: "OWNER@",
			expectedResp:   nil,
			expectedErr:    "no active connections",
		},
		"empty principal": {
			addr:         MockServers,
			expectedResp: nil,
			expectedErr:  "no principal provided",
		},
		"gRPC call failed": {
			addr:           MockServers,
			inputPrincipal: "OWNER@",
			deleteACLErr:   MockErr,
			expectedResp:   nil,
			expectedErr:    MockErr.Error(),
		},
		"gRPC resp bad status": {
			addr:                MockServers,
			inputPrincipal:      "OWNER@",
			deleteACLRespStatus: -5000,
			expectedResp:        nil,
			expectedErr:         "DAOS returned error code: -5000",
		},
		"success": {
			addr:           MockServers,
			inputPrincipal: "OWNER@",
			expectedResp:   &PoolDeleteACLResp{ACL: &AccessControlList{}},
			expectedErr:    "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var expectedACL []string
			if tt.expectedResp != nil {
				expectedACL = tt.expectedResp.ACL.Entries
			}
			aclResult := &mockACLResult{
				acl:    expectedACL,
				status: tt.deleteACLRespStatus,
				err:    tt.deleteACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, aclResult)

			req := PoolDeleteACLReq{
				UUID:      "TestUUID",
				Principal: tt.inputPrincipal,
			}

			resp, err := cc.PoolDeleteACL(req)

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
