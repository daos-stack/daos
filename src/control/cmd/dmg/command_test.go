//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
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

func (tc *testConn) DevStateQuery(req *mgmtpb.DevStateReq) client.ResultStateMap {
	tc.appendInvocation(fmt.Sprintf("DevStateQuery-%s", req))
	return nil
}

func (tc *testConn) StorageSetFaulty(req *mgmtpb.DevStateReq) client.ResultStateMap {
	tc.appendInvocation(fmt.Sprintf("StorageSetFaulty-%s", req))
	return nil
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

func createTestConfig(t *testing.T, log logging.Logger, path string) (*os.File, func()) {
	t.Helper()

	defaultConfig := client.NewConfiguration()
	if err := defaultConfig.SetPath(path); err != nil {
		t.Fatal(err)
	}

	// create default config file
	if err := os.MkdirAll(filepath.Dir(defaultConfig.Path), 0755); err != nil {
		t.Fatal(err)
	}
	f, err := os.Create(defaultConfig.Path)
	if err != nil {
		os.RemoveAll(filepath.Dir(defaultConfig.Path))
		t.Fatal(err)
	}
	cleanup := func() {
		os.RemoveAll(filepath.Dir(defaultConfig.Path))
	}

	return f, cleanup
}

func runCmd(t *testing.T, cmd string, log *logging.LeveledLogger, ctlClient control.Invoker, conn client.Connect) error {
	t.Helper()

	var opts cliOptions
	args := append([]string{"--insecure"}, strings.Split(cmd, " ")...)
	return parseOpts(args, &opts, ctlClient, conn, log)
}

// printRequest generates a stable string representation of the
// supplied UnaryRequest. It only includes exported fields in
// the output.
func printRequest(t *testing.T, req control.UnaryRequest) string {
	buf, err := json.Marshal(req)
	if err != nil {
		t.Fatalf("unable to print %+v: %s", req, err)
	}
	return fmt.Sprintf("%T-%s", req, string(buf))
}

// bridgeConnInvoker is a temporary bridge between old-style client.Connection
// requests and new-style control API requests. It is intended to ease transition
// to the new control API without requiring a complete rewrite of all tests.
type bridgeConnInvoker struct {
	control.MockInvoker
	t    *testing.T
	conn *testConn
}

func (bci *bridgeConnInvoker) InvokeUnaryRPC(ctx context.Context, uReq control.UnaryRequest) (*control.UnaryResponse, error) {
	// Use the testConn to fill out the calls slice for compatiblity
	// with old-style Connection tests.
	bci.conn.ConnectClients(nil)
	bci.conn.appendInvocation(printRequest(bci.t, uReq))

	// Synthesize a response as necessary. The dmg command tests
	// that interact with the MS will need a valid-ish MS response
	// in order to avoid failing response validation.
	resp := &control.UnaryResponse{}
	switch uReq.(type) {
	case *control.PoolCreateReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolCreateResp{})
	case *control.PoolDestroyReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolDestroyResp{})
	case *control.PoolSetPropReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolSetPropResp{
			Property: &mgmtpb.PoolSetPropResp_Name{},
			Value:    &mgmtpb.PoolSetPropResp_Numval{},
		})
	case *control.SystemStopReq:
		resp = control.MockMSResponse("", nil, &ctlpb.SystemStopResp{})
	case *control.SystemStartReq:
		resp = control.MockMSResponse("", nil, &ctlpb.SystemStartResp{})
	case *control.SystemQueryReq:
		resp = control.MockMSResponse("", nil, &ctlpb.SystemQueryResp{})
	case *control.LeaderQueryReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.LeaderQueryResp{})
	case *control.ListPoolsReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.ListPoolsResp{})
	case *control.ContSetOwnerReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.ContSetOwnerResp{})
	case *control.PoolGetACLReq, *control.PoolOverwriteACLReq,
		*control.PoolUpdateACLReq, *control.PoolDeleteACLReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.ACLResp{})
	case *control.PoolReintegrateReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolReintegrateResp{})
	}

	return resp, nil
}

func runCmdTests(t *testing.T, cmdTests []cmdTest) {
	t.Helper()

	for _, st := range cmdTests {
		t.Run(st.name, func(t *testing.T) {
			t.Helper()
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			f, cleanup := createTestConfig(t, log, "")
			f.Close()
			defer cleanup()

			ctlClient := control.DefaultMockInvoker(log)
			conn := newTestConn(t)
			bridge := &bridgeConnInvoker{
				MockInvoker: *ctlClient,
				t:           t,
				conn:        conn,
			}
			err := runCmd(t, st.cmd, log, bridge, conn)
			if err != st.expectedErr {
				if st.expectedErr == nil {
					t.Fatalf("expected nil error, got %+v", err)
				}

				if err == nil {
					t.Fatalf("expected err '%v', got nil", st.expectedErr)
				}

				testExpectedError(t, st.expectedErr, err)
				return
			}
			if st.expectedCalls != "" {
				st.expectedCalls = fmt.Sprintf("SetTransportConfig %s", st.expectedCalls)
			}

			if diff := cmp.Diff(st.expectedCalls, strings.Join(conn.called, " ")); diff != "" {
				t.Fatalf("unexpected function calls (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBadCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	var opts cliOptions
	conn := newTestConn(t)
	err := parseOpts([]string{"foo"}, &opts, nil, conn, log)
	testExpectedError(t, fmt.Errorf("Unknown command `foo'"), err)
}

func TestNoCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	var opts cliOptions
	conn := newTestConn(t)
	err := parseOpts([]string{}, &opts, nil, conn, log)
	testExpectedError(t, fmt.Errorf("Please specify one command"), err)
}
