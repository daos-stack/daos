//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"net"
	"sync"
	"testing"
	"time"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

// Utilities for internal server package tests

// mockDrpcClientConfig is a configuration structure for mockDrpcClient
type mockDrpcClientConfig struct {
	IsConnectedBool     bool
	ConnectError        error
	CloseError          error
	SendMsgResponseList []*drpc.Response
	SendMsgErrors       []error
	SendMsgResponse     *drpc.Response
	SendMsgError        error
	ResponseDelay       time.Duration
	SocketPath          string
}

type mockDrpcResponse struct {
	Status  drpc.Status
	Message proto.Message
	Error   error
}

func (cfg *mockDrpcClientConfig) setSendMsgResponseList(t *testing.T, mocks ...*mockDrpcResponse) {
	for _, mock := range mocks {
		body, err := proto.Marshal(mock.Message)
		if err != nil {
			t.Fatal(err)
		}
		response := &drpc.Response{
			Status: mock.Status,
			Body:   body,
		}
		cfg.SendMsgResponseList = append(cfg.SendMsgResponseList, response)
		cfg.SendMsgErrors = append(cfg.SendMsgErrors, mock.Error)
	}
}

func (cfg *mockDrpcClientConfig) setSendMsgResponse(status drpc.Status, body []byte, err error) {
	cfg.SendMsgResponse = &drpc.Response{
		Status: status,
		Body:   body,
	}
	cfg.SendMsgError = err
}

func (cfg *mockDrpcClientConfig) setResponseDelay(duration time.Duration) {
	cfg.ResponseDelay = duration
}

type mockDrpcCall struct {
	Method drpc.Method
	Body   []byte
}

func (c *mockDrpcCall) String() string {
	return fmt.Sprintf("%s: (%v)", c.Method, c.Body)
}

type mockDrpcCalls struct {
	sync.RWMutex
	calls []*mockDrpcCall
}

func (c *mockDrpcCalls) add(call *mockDrpcCall) {
	c.Lock()
	defer c.Unlock()
	c.calls = append(c.calls, call)
}

func (c *mockDrpcCalls) get() []*mockDrpcCall {
	c.RLock()
	defer c.RUnlock()
	return c.calls
}

// mockDrpcClient is a mock of the DomainSocketClient interface
type mockDrpcClient struct {
	sync.Mutex
	cfg              mockDrpcClientConfig
	CloseCallCount   int
	SendMsgInputCall *drpc.Call
	calls            mockDrpcCalls
}

func (c *mockDrpcClient) IsConnected() bool {
	return c.cfg.IsConnectedBool
}

func (c *mockDrpcClient) Connect(ctx context.Context) error {
	if err := ctx.Err(); err != nil {
		return err
	}
	return c.cfg.ConnectError
}

func (c *mockDrpcClient) Close() error {
	c.CloseCallCount++
	return c.cfg.CloseError
}

func (c *mockDrpcClient) CalledMethods() (methods []drpc.Method) {
	for _, call := range c.calls.get() {
		methods = append(methods, call.Method)
	}
	return
}

func (c *mockDrpcClient) SendMsg(_ context.Context, call *drpc.Call) (*drpc.Response, error) {
	c.SendMsgInputCall = call
	method, err := drpc.ModuleMgmt.GetMethod(call.GetMethod())
	if err != nil {
		return nil, err
	}
	c.calls.add(&mockDrpcCall{method, call.Body})

	<-time.After(c.cfg.ResponseDelay)

	if len(c.cfg.SendMsgResponseList) > 0 {
		idx := len(c.calls.get()) - 1
		if idx < 0 {
			idx = 0
		}
		if idx < len(c.cfg.SendMsgResponseList) {
			return c.cfg.SendMsgResponseList[idx], c.cfg.SendMsgErrors[idx]
		}
	}
	return c.cfg.SendMsgResponse, c.cfg.SendMsgError
}

func (c *mockDrpcClient) GetSocketPath() string {
	return c.cfg.SocketPath
}

func newMockDrpcClient(cfg *mockDrpcClientConfig) *mockDrpcClient {
	if cfg == nil {
		cfg = &mockDrpcClientConfig{}
	}

	return &mockDrpcClient{cfg: *cfg}
}

// setupMockDrpcClientBytes sets up the dRPC client for the mgmtSvc to return
// a set of bytes as a response.
func setupMockDrpcClientBytes(svc *mgmtSvc, respBytes []byte, err error) {
	mi := svc.harness.instances[0]
	cfg := &mockDrpcClientConfig{}
	cfg.setSendMsgResponse(drpc.Status_SUCCESS, respBytes, err)
	mi.(*EngineInstance).setDrpcClient(newMockDrpcClient(cfg))
}

// setupMockDrpcClient sets up the dRPC client for the mgmtSvc to return
// a valid protobuf message as a response.
func setupMockDrpcClient(svc *mgmtSvc, resp proto.Message, err error) {
	respBytes, _ := proto.Marshal(resp)
	setupMockDrpcClientBytes(svc, respBytes, err)
}

// newTestEngine returns an EngineInstance configured for testing.
func newTestEngine(log logging.Logger, isAP bool, provider *storage.Provider, engineCfg ...*engine.Config) *EngineInstance {
	if len(engineCfg) == 0 {
		engineCfg = append(engineCfg, engine.MockConfig().
			WithTargetCount(1).
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("nvme").
					WithBdevDeviceList("foo", "bar"),
			),
		)
	}
	rCfg := new(engine.TestRunnerConfig)
	rCfg.Running.SetTrue()
	r := engine.NewTestRunner(rCfg, engineCfg[0])

	srv := NewEngineInstance(log, provider, nil, r)
	srv.setSuperblock(&Superblock{
		Rank: ranklist.NewRankPtr(0),
	})
	srv.ready.SetTrue()
	srv.OnReady()

	return srv
}

// mockTCPResolver returns successful resolve results for any input.
func mockTCPResolver(netString string, address string) (*net.TCPAddr, error) {
	if netString != "tcp" {
		return nil, errors.Errorf("unexpected network type in test: %s, want 'tcp'", netString)
	}

	return &net.TCPAddr{IP: net.ParseIP("127.0.0.1"), Port: 10001}, nil
}

// newTestMgmtSvc creates a mgmtSvc that contains an EngineInstance
// properly set up as an MS.
func newTestMgmtSvc(t *testing.T, log logging.Logger) *mgmtSvc {
	harness := NewEngineHarness(log)
	provider := storage.MockProvider(log, 0, nil, nil, nil, nil, nil)

	srv := newTestEngine(log, true, provider)

	if err := harness.AddInstance(srv); err != nil {
		t.Fatal(err)
	}
	harness.started.SetTrue()

	db := raft.MockDatabase(t, log)
	ms := system.MockMembership(t, log, db, mockTCPResolver)
	ctx := test.Context(t)
	svc := newMgmtSvc(harness, ms, db, nil, events.NewPubSub(ctx, log))
	svc.batchInterval = 100 * time.Microsecond // Speed up tests
	svc.startAsyncLoops(ctx)
	svc.startLeaderLoops(ctx)
	return svc
}

// newTestMgmtSvcMulti creates a mgmtSvc that contains the requested
// number of EngineInstances. If requested, the first instance is
// configured as an access point.
func newTestMgmtSvcMulti(t *testing.T, log logging.Logger, count int, isAP bool) *mgmtSvc {
	harness := NewEngineHarness(log)
	provider := storage.MockProvider(log, 0, nil, nil, nil, nil, nil)

	for i := 0; i < count; i++ {
		srv := newTestEngine(log, i == 0 && isAP, provider)
		srv._superblock.Rank = ranklist.NewRankPtr(uint32(i))

		if err := harness.AddInstance(srv); err != nil {
			t.Fatal(err)
		}
	}
	harness.started.SetTrue()

	svc := newTestMgmtSvc(t, log)
	svc.harness = harness

	return svc
}

// newTestMgmtSvcNonReplica creates a mgmtSvc that is configured to
// fail if operations expect it to be a replica.
func newTestMgmtSvcNonReplica(t *testing.T, log logging.Logger) *mgmtSvc {
	svc := newTestMgmtSvc(t, log)
	svc.sysdb = raft.MockDatabaseWithAddr(t, log, nil)
	return svc
}
