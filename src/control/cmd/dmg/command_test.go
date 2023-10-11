//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
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
	t      *testing.T
	called []string
}

func newTestConn(t *testing.T) *testConn {
	return &testConn{
		t: t,
	}
}

func (tc *testConn) appendInvocation(name string) {
	tc.called = append(tc.called, name)
}

func testExpectedError(t *testing.T, expected, actual error) {
	t.Helper()

	errRe := regexp.MustCompile(expected.Error())
	if !errRe.MatchString(actual.Error()) {
		t.Fatalf("error string %q doesn't match expected error %q", actual, expected)
	}
}

func runCmd(t *testing.T, cmd string, log *logging.LeveledLogger, ctlClient control.Invoker) error {
	t.Helper()

	var opts cliOptions
	args := append([]string{"--insecure"}, strings.Split(cmd, " ")...)
	return parseOpts(args, &opts, ctlClient, log)
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
	// Use the testConn to fill out the calls slice for compatibility
	// with old-style Connection tests.
	bci.conn.appendInvocation(printRequest(bci.t, uReq))

	// Synthesize a response as necessary. The dmg command tests
	// that interact with the MS will need a valid-ish MS response
	// in order to avoid failing response validation.
	resp := &control.UnaryResponse{}
	switch req := uReq.(type) {
	case *control.PoolCreateReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolCreateResp{
			TierBytes: []uint64{0},
			TgtRanks:  []uint32{0},
		})
	case *control.PoolDestroyReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolDestroyResp{})
	case *control.PoolEvictReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolEvictResp{})
	case *control.PoolSetPropReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolSetPropResp{})
	case *control.PoolGetPropReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolGetPropResp{
			Properties: []*mgmtpb.PoolProperty{
				{
					Number: 1,
					Value: &mgmtpb.PoolProperty_Strval{
						Strval: "foo",
					},
				},
			},
		})
	case *control.SystemStopReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemStopResp{})
	case *control.SystemEraseReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemEraseResp{})
	case *control.SystemStartReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemStartResp{})
	case *control.SystemExcludeReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemExcludeResp{})
	case *control.SystemQueryReq:
		if req.FailOnUnavailable {
			resp = control.MockMSResponse("", system.ErrRaftUnavail, nil)
			break
		}
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemQueryResp{})
	case *control.SystemCleanupReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemCleanupResp{})
	case *control.LeaderQueryReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.LeaderQueryResp{})
	case *control.ListPoolsReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.ListPoolsResp{})
	case *control.ContSetOwnerReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.ContSetOwnerResp{})
	case *control.PoolQueryReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolQueryResp{})
	case *control.PoolQueryTargetReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolQueryTargetResp{})
	case *control.PoolUpgradeReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolUpgradeResp{})
	case *control.PoolGetACLReq, *control.PoolOverwriteACLReq,
		*control.PoolUpdateACLReq, *control.PoolDeleteACLReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.ACLResp{})
	case *control.PoolExcludeReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolExcludeResp{})
	case *control.PoolDrainReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolDrainResp{})
	case *control.PoolExtendReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolExtendResp{})
	case *control.PoolReintegrateReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolReintegrateResp{})
	case *control.SystemSetAttrReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemGetAttrReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemGetAttrResp{})
	case *control.SystemSetPropReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemGetPropReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemGetPropResp{})
	}

	return resp, nil
}

func runCmdTests(t *testing.T, cmdTests []cmdTest) {
	t.Helper()

	for _, st := range cmdTests {
		t.Run(st.name, func(t *testing.T) {
			t.Helper()
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctlClient := control.DefaultMockInvoker(log)
			conn := newTestConn(t)
			bridge := &bridgeConnInvoker{
				MockInvoker: *ctlClient,
				t:           t,
				conn:        conn,
			}
			err := runCmd(t, st.cmd, log, bridge)
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
			if diff := cmp.Diff(st.expectedCalls, strings.Join(conn.called, " ")); diff != "" {
				t.Fatalf("unexpected function calls (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBadCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	var opts cliOptions
	err := parseOpts([]string{"foo"}, &opts, nil, log)
	testExpectedError(t, fmt.Errorf("Unknown command `foo'"), err)
}

func TestNoCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	var opts cliOptions
	err := parseOpts([]string{}, &opts, nil, log)
	testExpectedError(t, fmt.Errorf("Please specify one command"), err)
}
