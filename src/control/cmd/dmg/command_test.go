//
// (C) Copyright 2019 Intel Corporation.
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
	"regexp"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

type dmgTestErr string

func (dte dmgTestErr) Error() string {
	return string(dte)
}

const (
	errMissingFlag = dmgTestErr("required flag")
)

type cmdTest struct {
	name          string
	cmd           string
	expectedCalls string
	expectedErr   error
}

type testConn struct {
	t            *testing.T
	clientConfig *client.Configuration
	called       []string
}

func newTestConn(t *testing.T) *testConn {
	cfg := client.NewConfiguration()
	return &testConn{
		clientConfig: cfg,
		t:            t,
	}
}

func (tc *testConn) appendInvocation(name string) {
	tc.called = append(tc.called, name)
}

func (tc *testConn) ConnectClients(addrList client.Addresses) client.ResultMap {
	tc.appendInvocation("ConnectClients")

	return map[string]client.ClientResult{
		tc.clientConfig.HostList[0]: client.ClientResult{
			Address: tc.clientConfig.HostList[0],
		},
	}
}

func (tc *testConn) GetActiveConns(rm client.ResultMap) client.ResultMap {
	tc.appendInvocation("GetActiveConns")

	return map[string]client.ClientResult{
		tc.clientConfig.HostList[0]: client.ClientResult{
			Address: tc.clientConfig.HostList[0],
		},
	}
}

func (tc *testConn) ClearConns() client.ResultMap {
	tc.appendInvocation("ClearConns")
	return nil
}

func (tc *testConn) StoragePrepare(req *ctlpb.StoragePrepareReq) client.ResultMap {
	tc.appendInvocation("StoragePrepare")
	return nil
}

func (tc *testConn) StorageScan(req *client.StorageScanReq) *client.StorageScanResp {
	tc.appendInvocation(fmt.Sprintf("StorageScan-%+v", req))
	return &client.StorageScanResp{}
}

func (tc *testConn) StorageFormat(reformat bool) (client.ClientCtrlrMap, client.ClientMountMap) {
	tc.appendInvocation(fmt.Sprintf("StorageFormat-%t", reformat))
	return nil, nil
}

func (tc *testConn) KillRank(rank uint32) client.ResultMap {
	tc.appendInvocation(fmt.Sprintf("KillRank-rank %d", rank))
	return nil
}

func (tc *testConn) PoolCreate(req *client.PoolCreateReq) (*client.PoolCreateResp, error) {
	tc.appendInvocation(fmt.Sprintf("PoolCreate-%+v", req))
	return &client.PoolCreateResp{}, nil
}

func (tc *testConn) PoolDestroy(req *client.PoolDestroyReq) error {
	tc.appendInvocation(fmt.Sprintf("PoolDestroy-%+v", req))
	return nil
}

func (tc *testConn) PoolGetACL(req *client.PoolGetACLReq) (*client.PoolGetACLResp, error) {
	tc.appendInvocation(fmt.Sprintf("PoolGetACL-%+v", req))
	return &client.PoolGetACLResp{}, nil
}

func (tc *testConn) PoolOverwriteACL(req *client.PoolOverwriteACLReq) (*client.PoolOverwriteACLResp, error) {
	tc.appendInvocation(fmt.Sprintf("PoolOverwriteACL-%+v", req))
	return &client.PoolOverwriteACLResp{ACL: req.ACL}, nil
}

func (tc *testConn) BioHealthQuery(req *mgmtpb.BioHealthReq) client.ResultQueryMap {
	tc.appendInvocation(fmt.Sprintf("BioHealthQuery-%s", req))
	return nil
}

func (tc *testConn) SmdListDevs(req *mgmtpb.SmdDevReq) client.ResultSmdMap {
	tc.appendInvocation(fmt.Sprintf("SmdListDevs-%s", req))
	return nil
}

func (tc *testConn) SmdListPools(req *mgmtpb.SmdPoolReq) client.ResultSmdMap {
	tc.appendInvocation(fmt.Sprintf("SmdListPools-%s", req))
	return nil
}

func (tc *testConn) SystemMemberQuery() (common.SystemMembers, error) {
	tc.appendInvocation("SystemMemberQuery")
	return make(common.SystemMembers, 0), nil
}

func (tc *testConn) SystemStop() (common.SystemMemberResults, error) {
	tc.appendInvocation("SystemStop")
	return make(common.SystemMemberResults, 0), nil
}

func (tc *testConn) SetTransportConfig(cfg *security.TransportConfig) {
	tc.appendInvocation("SetTransportConfig")
}

func (tc *testConn) NetworkListProviders() client.ResultMap {
	tc.appendInvocation("NetworkListProviders")
	return nil
}

func (tc *testConn) NetworkScanDevices(searchProvider string) client.NetworkScanResultMap {
	tc.appendInvocation(fmt.Sprintf("NetworkScanDevices-%s", searchProvider))
	return nil
}

func testExpectedError(t *testing.T, expected, actual error) {
	t.Helper()

	errRe := regexp.MustCompile(expected.Error())
	if !errRe.MatchString(actual.Error()) {
		t.Fatalf("error string %q doesn't match expected error %q", actual, expected)
	}
}

func runCmdTests(t *testing.T, cmdTests []cmdTest) {
	t.Helper()

	for _, st := range cmdTests {
		t.Run(st.name, func(t *testing.T) {
			t.Helper()
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			var opts cliOptions
			conn := newTestConn(t)
			args := append([]string{"--insecure"}, strings.Split(st.cmd, " ")...)
			err := parseOpts(args, &opts, conn, log)
			if err != st.expectedErr {
				if st.expectedErr == nil {
					t.Fatalf("expected nil error, got %+v", err)
				}

				testExpectedError(t, st.expectedErr, err)
				return
			}
			if st.expectedCalls != "" {
				st.expectedCalls = fmt.Sprintf("SetTransportConfig %s", st.expectedCalls)
			}
			common.AssertEqual(t, strings.Join(conn.called, " "), st.expectedCalls,
				"called functions do not match expected calls")
		})
	}
}

func TestBadCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	var opts cliOptions
	conn := newTestConn(t)
	err := parseOpts([]string{"foo"}, &opts, conn, log)
	testExpectedError(t, fmt.Errorf("Unknown command `foo'"), err)
}

func TestNoCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	var opts cliOptions
	conn := newTestConn(t)
	err := parseOpts([]string{}, &opts, conn, log)
	testExpectedError(t, fmt.Errorf("Please specify one command"), err)
}
