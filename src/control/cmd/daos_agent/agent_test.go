//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

const (
	childModeEnvVar        = "GO_TESTING_CHILD_MODE"
	childModeGetAttachInfo = "MODE_GET_ATTACH_INFO"
	drpcDirEnvVar          = "DAOS_AGENT_DRPC_DIR"
)

func childErrExit(err error) {
	if err == nil {
		err = errors.New("unknown error")
	}
	fmt.Fprintf(os.Stderr, "CHILD ERROR: %s\n", err)
	os.Exit(1)
}

func getAttachInfo() error {
	sockPath := os.Getenv(drpcDirEnvVar) + "/" + agentSockName
	drpcClient := drpc.NewClientConnection(sockPath)
	if err := drpcClient.Connect(); err != nil {
		return err
	}
	defer drpcClient.Close()

	req := &mgmtpb.GetAttachInfoReq{}

	var bodyBytes []byte
	var err error
	bodyBytes, err = proto.Marshal(req)
	if err != nil {
		return err
	}

	method := drpc.MethodGetAttachInfo
	call := &drpc.Call{
		Module: method.Module().ID(),
		Method: method.ID(),
		Body:   bodyBytes,
	}

	dresp, err := drpcClient.SendMsg(call)
	if err != nil {
		return err
	}

	attachInfo := new(mgmtpb.GetAttachInfoResp)
	if err = proto.Unmarshal(dresp.Body, attachInfo); err != nil {
		return err
	}

	payload, err := json.Marshal(attachInfo)
	if err != nil {
		return err
	}

	res := pbin.Response{
		Payload: payload,
	}

	conn := pbin.NewStdioConn("child", "parent", os.Stdin, os.Stdout)

	writeBuf, err := json.Marshal(&res)
	if err != nil {
		return err
	}

	if _, err = conn.Write(writeBuf); err != nil {
		return err
	}

	if err = conn.CloseWrite(); err != nil {
		return err
	}

	return nil
}

func TestMain(m *testing.M) {
	mode := os.Getenv(childModeEnvVar)
	switch mode {
	case "":
		// default; run the test binary
		os.Exit(m.Run())
	case childModeGetAttachInfo:
		if err := getAttachInfo(); err != nil {
			childErrExit(err)
		}
	default:
		childErrExit(errors.Errorf("Unknown child mode: %q", mode))
	}
}

func TestAgent_MultiProcess_AttachInfoCache(t *testing.T) {
	t.Skip("DAOS-8967")
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	srvResp := &mgmtpb.GetAttachInfoResp{
		ClientNetHint: &mgmtpb.ClientNetHint{
			Provider:    "tcp+sockets",
			NetDevClass: uint32(hardware.Ether),
		},
	}

	agentCfg := DefaultConfig()
	agentCfg.RuntimeDir = tmpDir
	client := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("localhost", nil, srvResp),
	})
	testAgentStart := &startCmd{
		LogCmd: cmdutil.LogCmd{
			Logger: log,
		},
		configCmd: configCmd{
			cfg: agentCfg,
		},
		ctlInvokerCmd: ctlInvokerCmd{
			ctlInvoker: client,
		},
	}

	// Start the "agent" in a goroutine. Unfortunately, we can't block
	// on waiting for it to be ready without rewriting things a lot, but
	// sleeping for a second should be fine.
	go func() {
		err := testAgentStart.Execute([]string{})
		if err != nil {
			t.Log(err)
		}
	}()
	time.Sleep(1 * time.Second)

	errCh := make(chan error)
	respCh := make(chan *mgmtpb.GetAttachInfoResp)

	// Now, start a bunch of child processes to hammer away at the agent. The goal
	// is to verify that the cache should be initialized with the result of a single
	// GetAttachInfo RPC invocation.
	os.Setenv(drpcDirEnvVar, tmpDir)
	os.Setenv(childModeEnvVar, childModeGetAttachInfo)
	maxIter := 32
	for i := 0; i < maxIter; i++ {
		go func(rc chan *mgmtpb.GetAttachInfoResp, ec chan error) {
			pr, err := pbin.ExecReq(context.Background(), log, os.Args[0], &pbin.Request{})
			if err != nil {
				ec <- err
				return
			}

			gr := new(mgmtpb.GetAttachInfoResp)
			if err = json.Unmarshal(pr.Payload, gr); err != nil {
				ec <- err
				return
			}
			rc <- gr
		}(respCh, errCh)
	}

	// We don't really care about the interface chosen for this test; the main
	// thing we want to verify is that the responses look valid and that the
	// cache is working correctly (i.e. only 1 invocation of the RPC).
	cmpOpts := append(test.DefaultCmpOpts(),
		protocmp.IgnoreFields(&mgmtpb.ClientNetHint{}, "domain", "interface"))
	for i := 0; i < maxIter; i++ {
		select {
		case err := <-errCh:
			t.Fatal(err)
		case resp := <-respCh:
			if diff := cmp.Diff(resp, srvResp, cmpOpts...); diff != "" {
				t.Errorf("Unexpected resp (-want +got):\n%s", diff)
			}
		}
	}

	test.AssertEqual(t, client.GetInvokeCount(), 1, "unexpected number of GetAttachInfo RPC invocations")
}
