//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"gopkg.in/yaml.v2"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
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

	tmpCfg := control.DefaultConfig()
	tmpCfg.TransportConfig.AllowInsecure = true
	tmpCfgDir := t.TempDir()
	tmpCfgFile := path.Join(tmpCfgDir, "control_config.yaml")
	tmpCfgYaml, err := yaml.Marshal(tmpCfg)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(tmpCfgFile, tmpCfgYaml, 0600); err != nil {
		t.Fatal(err)
	}

	var opts cliOptions
	args := append([]string{"--config-path", tmpCfgFile}, strings.Split(cmd, " ")...)
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
	case *control.SystemDrainReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemDrainResp{})
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
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.PoolQueryReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolQueryResp{})
	case *control.PoolQueryTargetReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolQueryTargetResp{})
	case *control.PoolUpgradeReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolUpgradeResp{})
	case *control.PoolGetACLReq, *control.PoolOverwriteACLReq,
		*control.PoolUpdateACLReq, *control.PoolDeleteACLReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.ACLResp{})
	case *control.PoolRanksReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolRanksResp{})
	case *control.PoolExtendReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.PoolExtendResp{})
	case *control.SystemCheckEnableReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemCheckDisableReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemCheckStartReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemCheckStopReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemCheckQueryReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.CheckQueryResp{})
	case *control.SystemCheckGetPolicyReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.CheckGetPolicyResp{})
	case *control.SystemCheckSetPolicyReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemCheckRepairReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemSetAttrReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemGetAttrReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemGetAttrResp{})
	case *control.SystemSetPropReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.DaosResp{})
	case *control.SystemGetPropReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.SystemGetPropResp{})
	case *control.GetAttachInfoReq:
		resp = control.MockMSResponse("", nil, &mgmtpb.GetAttachInfoResp{})
	case *control.NetworkScanReq:
		resp = &control.UnaryResponse{
			Responses: []*control.HostResponse{
				{
					Addr: "host1",
					Message: &ctlpb.NetworkScanResp{
						Interfaces: []*ctlpb.FabricInterface{
							{
								Provider:    "test-provider",
								Device:      "test-device",
								Netdevclass: uint32(hardware.Infiniband),
							},
						},
						Numacount:    1,
						Corespernuma: 2,
					},
				},
			},
		}
	case *control.StorageScanReq:
		resp = &control.UnaryResponse{
			Responses: []*control.HostResponse{
				{
					Addr: "host1",
					Message: &ctlpb.StorageScanResp{
						Scm: &ctlpb.ScanScmResp{
							Namespaces: []*ctlpb.ScmNamespace{
								{},
							},
						},
						Nvme: &ctlpb.ScanNvmeResp{
							Ctrlrs: []*ctlpb.NvmeController{
								{PciAddr: "0000:80:0.0"},
							},
						},
						MemInfo: &ctlpb.MemInfo{
							HugepageSizeKb: 2048,
						},
					},
				},
			},
		}
	}

	return resp, nil
}

func runCmdTest(t *testing.T, cmd, expectedCalls string, expectedErr error) {
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
	err := runCmd(t, cmd, log, bridge)
	if err != expectedErr {
		if expectedErr == nil {
			t.Fatalf("expected nil error, got %+v", err)
		}

		if err == nil {
			t.Fatalf("expected err '%v', got nil", expectedErr)
		}

		testExpectedError(t, expectedErr, err)
		return
	}
	if diff := cmp.Diff(expectedCalls, strings.Join(conn.called, " ")); diff != "" {
		t.Fatalf("unexpected function calls (-want, +got):\n%s\n", diff)
	}
}

func runCmdTests(t *testing.T, cmdTests []cmdTest) {
	t.Helper()

	for _, st := range cmdTests {
		t.Run(st.name, func(t *testing.T) {
			runCmdTest(t, st.cmd, st.expectedCalls, st.expectedErr)
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
