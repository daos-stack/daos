//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"sync"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/mgmt"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	. "github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	. "github.com/daos-stack/daos/src/control/system"
)

func getTestNotifyReadyReq(t *testing.T, sockPath string, idx uint32) *srvpb.NotifyReadyReq {
	return &srvpb.NotifyReadyReq{
		DrpcListenerSock: sockPath,
		InstanceIdx:      idx,
	}
}

func waitForEngineReady(t *testing.T, instance *EngineInstance) {
	select {
	case <-time.After(100 * time.Millisecond):
		t.Fatal("IO engine never became ready!")
	case <-instance.awaitDrpcReady():
		return
	}
}

func TestEngineInstance_NotifyDrpcReady(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	instance := getTestEngineInstance(log)

	req := getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0)

	instance.NotifyDrpcReady(req)

	test.AssertEqual(t, req.DrpcListenerSock, instance.getDrpcSocket(), "expected socket value set")

	waitForEngineReady(t, instance)
}

func TestEngineInstance_CallDrpc(t *testing.T) {
	for name, tc := range map[string]struct {
		notStarted bool
		notReady   bool
		noSocket   bool
		noClient   bool
		resp       *drpc.Response
		expErr     error
	}{
		"not started": {
			notStarted: true,
			expErr:     FaultDataPlaneNotStarted,
		},
		"not ready": {
			notReady: true,
			expErr:   errEngineNotReady,
		},
		"drpc not ready": {
			noSocket: true,
			expErr:   errDRPCNotReady,
		},
		"success": {
			resp: &drpc.Response{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			trc := engine.TestRunnerConfig{}
			trc.Running.Store(!tc.notStarted)
			runner := engine.NewTestRunner(&trc, engine.MockConfig())
			instance := NewEngineInstance(log, nil, nil, runner)
			instance.ready.Store(!tc.notReady)

			if !tc.noSocket {
				instance.setDrpcSocket("/something")
			}

			cfg := &mockDrpcClientConfig{
				SendMsgResponse: tc.resp,
			}
			instance.getDrpcClientFn = func(s string) drpc.DomainSocketClient {
				return newMockDrpcClient(cfg)
			}

			_, err := instance.CallDrpc(test.Context(t),
				drpc.MethodPoolCreate, &mgmtpb.PoolCreateReq{})
			test.CmpErr(t, tc.expErr, err)
		})
	}
}

type sendMsgDrpcClient struct {
	sync.Mutex
	sendMsgFn func(context.Context, *drpc.Call) (*drpc.Response, error)
}

func (c *sendMsgDrpcClient) IsConnected() bool {
	return true
}

func (c *sendMsgDrpcClient) Connect(_ context.Context) error {
	return nil
}

func (c *sendMsgDrpcClient) Close() error {
	return nil
}

func (c *sendMsgDrpcClient) SendMsg(ctx context.Context, call *drpc.Call) (*drpc.Response, error) {
	return c.sendMsgFn(ctx, call)
}

func (c *sendMsgDrpcClient) GetSocketPath() string {
	return ""
}

func TestEngineInstance_CallDrpc_Parallel(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// This test starts with one long-running drpc client that should remain in the SendMsg
	// function until all other clients complete, demonstrating a single dRPC call cannot
	// block the channel.

	numClients := 100
	numFastClients := numClients - 1

	doneCh := make(chan struct{}, numFastClients)
	longClient := &sendMsgDrpcClient{
		sendMsgFn: func(ctx context.Context, _ *drpc.Call) (*drpc.Response, error) {
			numDone := 0

			for numDone < numFastClients {
				select {
				case <-ctx.Done():
					t.Fatalf("context done before test finished: %s", ctx.Err())
				case <-doneCh:
					numDone++
					t.Logf("%d/%d finished", numDone, numFastClients)
				}
			}
			t.Log("long running client finished")
			return &drpc.Response{}, nil
		},
	}

	clientCh := make(chan drpc.DomainSocketClient, numClients)
	go func(t *testing.T) {
		t.Log("starting client producer thread...")
		t.Log("adding long-running client")
		clientCh <- longClient
		for i := 0; i < numFastClients; i++ {
			t.Logf("adding client %d", i)
			clientCh <- &sendMsgDrpcClient{
				sendMsgFn: func(ctx context.Context, _ *drpc.Call) (*drpc.Response, error) {
					doneCh <- struct{}{}
					return &drpc.Response{}, nil
				},
			}
		}
		t.Log("closing client channel")
		close(clientCh)
	}(t)

	t.Log("setting up engine...")
	trc := engine.TestRunnerConfig{}
	trc.Running.Store(true)
	runner := engine.NewTestRunner(&trc, engine.MockConfig())
	instance := NewEngineInstance(log, nil, nil, runner)
	instance.ready.Store(true)

	instance.getDrpcClientFn = func(s string) drpc.DomainSocketClient {
		t.Log("fetching drpc client")
		cli := <-clientCh
		t.Log("got drpc client")
		return cli
	}

	var wg sync.WaitGroup
	wg.Add(numClients)
	for i := 0; i < numClients; i++ {
		go func(t *testing.T, j int) {
			t.Logf("%d: CallDrpc", j)
			_, err := instance.CallDrpc(test.Context(t), drpc.MethodPoolCreate, &mgmt.PoolCreateReq{})
			if err != nil {
				t.Logf("%d: error: %s", j, err.Error())
			}
			wg.Done()
		}(t, i)
	}
	wg.Wait()
}

func TestEngineInstance_DrespToRankResult(t *testing.T) {
	dRank := Rank(1)

	for name, tc := range map[string]struct {
		daosResp    *mgmtpb.DaosResp
		inErr       error
		targetState MemberState
		junkRPC     bool
		expResult   *MemberResult
	}{
		"rank success": {
			expResult: &MemberResult{Rank: dRank, State: MemberStateJoined},
		},
		"rank failure": {
			daosResp: &mgmtpb.DaosResp{Status: int32(daos.NoSpace)},
			expResult: &MemberResult{
				Rank: dRank, State: MemberStateErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d: %s", dRank, daos.NoSpace),
			},
		},
		"drpc failure": {
			inErr: errors.New("returned from CallDrpc"),
			expResult: &MemberResult{
				Rank: dRank, State: MemberStateErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC failed: returned from CallDrpc", dRank),
			},
		},
		"unmarshal failure": {
			junkRPC: true,
			expResult: &MemberResult{
				Rank: dRank, State: MemberStateErrored, Errored: true,
				Msg: fmt.Sprintf("rank %d dRPC unmarshal failed", dRank),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.daosResp == nil {
				tc.daosResp = &mgmtpb.DaosResp{Status: 0}
			}
			if tc.targetState == MemberStateUnknown {
				tc.targetState = MemberStateJoined
			}

			// convert input DaosResp to drpcResponse to test
			rb := makeBadBytes(42)
			if !tc.junkRPC {
				rb, _ = proto.Marshal(tc.daosResp)
			}
			resp := &drpc.Response{
				Status: drpc.Status_SUCCESS, // this will already have been validated by CallDrpc
				Body:   rb,
			}

			gotResult := drespToMemberResult(Rank(dRank), resp, tc.inErr, tc.targetState)
			if diff := cmp.Diff(tc.expResult, gotResult, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
