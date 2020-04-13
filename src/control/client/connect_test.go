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
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	. "google.golang.org/grpc/connectivity"

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

func connectSetupServers(
	servers Addresses, log logging.Logger, state State, ctrlrs NvmeControllers,
	ctrlrResults NvmeControllerResults, modules ScmModules,
	moduleResults ScmModuleResults, pmems ScmNamespaces, mountResults ScmMountResults,
	scanRet error, formatRet error, killRet error, connectRet error,
	ACLRet *MockACLResult, listPoolsRet *MockListPoolsResult) Connect {

	connect := newMockConnect(
		log, state, ctrlrs, ctrlrResults, modules,
		moduleResults, pmems, mountResults, scanRet, formatRet,
		killRet, connectRet, ACLRet, listPoolsRet)

	_ = connect.ConnectClients(servers)

	return connect
}

func connectSetup(
	log logging.Logger,
	state State, ctrlrs NvmeControllers, ctrlrResults NvmeControllerResults,
	modules ScmModules, moduleResults ScmModuleResults, pmems ScmNamespaces,
	mountResults ScmMountResults, scanRet error, formatRet error,
	killRet error, connectRet error, ACLRet *MockACLResult,
	listPoolsRet *MockListPoolsResult) Connect {

	return connectSetupServers(MockServers, log, state, ctrlrs,
		ctrlrResults, modules, moduleResults, pmems, mountResults, scanRet,
		formatRet, killRet, connectRet, ACLRet, listPoolsRet)
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
			nil, nil, nil, tt.connRet, nil, nil)

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
			aclResult := &MockACLResult{
				Acl:    expectedACL,
				Status: tt.getACLRespStatus,
				Err:    tt.getACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, aclResult, nil)

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
		Entries: MockACL.Acl,
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
			aclResult := &MockACLResult{
				Acl:    expectedACL,
				Status: tt.overwriteACLRespStatus,
				Err:    tt.overwriteACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, aclResult, nil)

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
		Entries: MockACL.Acl,
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
			aclResult := &MockACLResult{
				Acl:    expectedACL,
				Status: tt.updateACLRespStatus,
				Err:    tt.updateACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, aclResult, nil)

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
			aclResult := &MockACLResult{
				Acl:    expectedACL,
				Status: tt.deleteACLRespStatus,
				Err:    tt.deleteACLErr,
			}
			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, aclResult, nil)

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

func TestListPools(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	for name, tt := range map[string]struct {
		addr                Addresses
		listPoolsRespStatus int32
		listPoolsErr        error
		expectedResp        *ListPoolsResp
		expectedErr         string
	}{
		"no service leader": {
			addr:         nil,
			expectedResp: nil,
			expectedErr:  "no active connections",
		},
		"gRPC call failed": {
			addr:         MockServers,
			listPoolsErr: MockErr,
			expectedResp: nil,
			expectedErr:  MockErr.Error(),
		},
		"gRPC resp bad status": {
			addr:                MockServers,
			listPoolsRespStatus: -1000,
			expectedResp:        nil,
			expectedErr:         "DAOS returned error code: -1000",
		},
		"success": {
			addr:                MockServers,
			listPoolsRespStatus: 0,
			expectedResp:        &ListPoolsResp{Pools: PoolDiscoveriesFromPB(MockPoolList)},
			expectedErr:         "",
		},
	} {
		t.Run(name, func(t *testing.T) {

			cc := connectSetupServers(tt.addr, log, Ready,
				MockCtrlrs, MockCtrlrResults, MockScmModules,
				MockModuleResults, MockScmNamespaces, MockMountResults,
				nil, nil, nil, nil, MockACL,
				&MockListPoolsResult{
					Err:    tt.listPoolsErr,
					Status: tt.listPoolsRespStatus,
				})

			resp, err := cc.ListPools(ListPoolsReq{})

			if tt.expectedErr != "" {
				ExpectError(t, err, tt.expectedErr, name)
			} else if err != nil {
				t.Fatalf("expected nil error, got %v", err)
			}

			if diff := cmp.Diff(tt.expectedResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPoolSetProp(t *testing.T) {
	const (
		testPropName          = "test-prop"
		testPropValStr        = "test-val"
		testPropValNum uint64 = 42
	)

	for name, tc := range map[string]struct {
		mc      *mockConnectConfig
		req     PoolSetPropReq
		expResp *PoolSetPropResp
		expErr  error
	}{
		"no active connections": {
			expErr: errors.New("no active connections"),
		},
		"set-prop fails": {
			mc: &mockConnectConfig{
				addresses: MockServers,
				svcClientCfg: MockMgmtSvcClientConfig{
					PoolSetPropErr: errors.New("set-prop failed"),
				},
			},
			req: PoolSetPropReq{
				UUID:     MockUUID(),
				Property: testPropName,
				Value:    testPropValStr,
			},
			expErr: errors.New("set-prop failed"),
		},
		"nonzero resp status": {
			mc: &mockConnectConfig{
				addresses: MockServers,
				svcClientCfg: MockMgmtSvcClientConfig{
					PoolSetPropResult: &mgmtpb.PoolSetPropResp{
						Status: -42,
					},
				},
			},
			req: PoolSetPropReq{
				UUID:     MockUUID(),
				Property: testPropName,
				Value:    testPropValStr,
			},
			expErr: errors.New("DAOS returned error code: -42"),
		},
		"empty request property": {
			mc: &mockConnectConfig{
				addresses: MockServers,
			},
			req: PoolSetPropReq{
				UUID:     MockUUID(),
				Property: "",
			},
			expErr: errors.New("invalid property name"),
		},
		"invalid request value": {
			mc: &mockConnectConfig{
				addresses: MockServers,
			},
			req: PoolSetPropReq{
				UUID:     MockUUID(),
				Property: testPropName,
			},
			expErr: errors.New("unhandled property value"),
		},
		"invalid response value": {
			mc: &mockConnectConfig{
				addresses: MockServers,
				svcClientCfg: MockMgmtSvcClientConfig{
					PoolSetPropResult: &mgmtpb.PoolSetPropResp{},
				},
			},
			req: PoolSetPropReq{
				UUID:     MockUUID(),
				Property: testPropName,
				Value:    testPropValNum,
			},
			expErr: errors.New("unable to represent response value"),
		},
		"successful string property": {
			mc: &mockConnectConfig{
				addresses: MockServers,
				svcClientCfg: MockMgmtSvcClientConfig{
					PoolSetPropResult: &mgmtpb.PoolSetPropResp{
						Property: &mgmtpb.PoolSetPropResp_Name{
							Name: testPropName,
						},
						Value: &mgmtpb.PoolSetPropResp_Strval{
							Strval: testPropValStr,
						},
					},
				},
			},
			req: PoolSetPropReq{
				UUID:     MockUUID(),
				Property: testPropName,
				Value:    testPropValStr,
			},
			expResp: &PoolSetPropResp{
				UUID:     MockUUID(),
				Property: testPropName,
				Value:    testPropValStr,
			},
		},
		"successful numeric property": {
			mc: &mockConnectConfig{
				addresses: MockServers,
				svcClientCfg: MockMgmtSvcClientConfig{
					PoolSetPropResult: &mgmtpb.PoolSetPropResp{
						Property: &mgmtpb.PoolSetPropResp_Name{
							Name: testPropName,
						},
						Value: &mgmtpb.PoolSetPropResp_Numval{
							Numval: testPropValNum,
						},
					},
				},
			},
			req: PoolSetPropReq{
				UUID:     MockUUID(),
				Property: testPropName,
				Value:    testPropValNum,
			},
			expResp: &PoolSetPropResp{
				UUID:     MockUUID(),
				Property: testPropName,
				Value:    strconv.FormatUint(testPropValNum, 10),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			c := newMockConnectCfg(log, tc.mc)
			gotResp, gotErr := c.PoolSetProp(tc.req)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
