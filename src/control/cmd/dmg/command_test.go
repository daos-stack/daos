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
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
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
	expectedOpts  *cliOptions
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

func (tc *testConn) StorageScan() (client.ClientCtrlrMap, client.ClientModuleMap) {
	tc.appendInvocation("StorageScan")
	return nil, nil
}

func (tc *testConn) StorageFormat() (client.ClientCtrlrMap, client.ClientMountMap) {
	tc.appendInvocation("StorageFormat")
	return nil, nil
}

func (tc *testConn) StorageUpdate(req *pb.StorageUpdateReq) (client.ClientCtrlrMap, client.ClientModuleMap) {
	tc.appendInvocation(fmt.Sprintf("StorageUpdate-%s", req))
	return nil, nil
}

func (tc *testConn) ListFeatures() client.ClientFeatureMap {
	tc.appendInvocation("ListFeatures")
	return nil
}

func (tc *testConn) KillRank(uuid string, rank uint32) client.ResultMap {
	tc.appendInvocation(fmt.Sprintf("KillRank-uuid %s, rank %d", uuid, rank))
	return nil
}

func (tc *testConn) CreatePool(req *pb.CreatePoolReq) client.ResultMap {
	tc.appendInvocation(fmt.Sprintf("CreatePool-%s", req))
	return nil
}

func (tc *testConn) DestroyPool(req *pb.DestroyPoolReq) client.ResultMap {
	tc.appendInvocation(fmt.Sprintf("DestroyPool-%s", req))
	return nil
}

func (tc *testConn) SetTransportConfig(cfg *security.TransportConfig) {
	tc.appendInvocation("SetTransportConfig")
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
			defer common.ShowLogOnFailure(t)()
			t.Helper()

			conn := newTestConn(t)
			args := append([]string{"--insecure"}, strings.Split(st.cmd, " ")...)
			opts, err := parseOpts(args, conn)
			if err != st.expectedErr {
				if st.expectedErr == nil {
					t.Fatalf("expected nil error, got %+v", err)
				}

				testExpectedError(t, st.expectedErr, err)
			}
			if st.expectedCalls != "" {
				st.expectedCalls = fmt.Sprintf("SetTransportConfig %s", st.expectedCalls)
			}
			common.AssertEqual(t, strings.Join(conn.called, " "), st.expectedCalls,
				"called functions do not match expected calls")
			common.AssertEqual(t, opts, st.expectedOpts,
				"parsed options do not match expected options")
		})
	}
}

func TestBadCommand(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	conn := newTestConn(t)
	_, err := parseOpts([]string{"foo"}, conn)
	testExpectedError(t, fmt.Errorf("Unknown command `foo'"), err)
}

func TestNoCommand(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	conn := newTestConn(t)
	_, err := parseOpts([]string{}, conn)
	testExpectedError(t, fmt.Errorf("Please specify one command"), err)
}
