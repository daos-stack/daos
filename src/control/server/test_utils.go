//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"net"
	"sync"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
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

// mockDrpcClient is a mock of the DomainSocketClient interface
type mockDrpcClient struct {
	sync.Mutex
	cfg              mockDrpcClientConfig
	CloseCallCount   int
	SendMsgInputCall *drpc.Call
	calls            []*mockDrpcCall
}

func (c *mockDrpcClient) IsConnected() bool {
	return c.cfg.IsConnectedBool
}

func (c *mockDrpcClient) Connect() error {
	return c.cfg.ConnectError
}

func (c *mockDrpcClient) Close() error {
	c.CloseCallCount++
	return c.cfg.CloseError
}

func (c *mockDrpcClient) CalledMethods() (methods []drpc.Method) {
	for _, call := range c.calls {
		methods = append(methods, call.Method)
	}
	return
}

func (c *mockDrpcClient) SendMsg(call *drpc.Call) (*drpc.Response, error) {
	c.SendMsgInputCall = call
	method, err := drpc.ModuleMgmt.GetMethod(call.GetMethod())
	if err != nil {
		return nil, err
	}
	c.calls = append(c.calls, &mockDrpcCall{method, call.Body})

	<-time.After(c.cfg.ResponseDelay)

	if len(c.cfg.SendMsgResponseList) > 0 {
		idx := len(c.calls) - 1
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
	mi.setDrpcClient(newMockDrpcClient(cfg))
}

// setupMockDrpcClient sets up the dRPC client for the mgmtSvc to return
// a valid protobuf message as a response.
func setupMockDrpcClient(svc *mgmtSvc, resp proto.Message, err error) {
	respBytes, _ := proto.Marshal(resp)
	setupMockDrpcClientBytes(svc, respBytes, err)
}

// newTestIOServer returns an IOServerInstance configured for testing.
func newTestIOServer(log logging.Logger, isAP bool, ioCfg ...*ioserver.Config) *IOServerInstance {
	if len(ioCfg) == 0 {
		ioCfg = append(ioCfg, ioserver.NewConfig().WithTargetCount(1))
	}
	r := ioserver.NewTestRunner(&ioserver.TestRunnerConfig{
		Running: atm.NewBool(true),
	}, ioCfg[0])

	srv := NewIOServerInstance(log, nil, nil, nil, r)
	srv.setSuperblock(&Superblock{
		Rank: system.NewRankPtr(0),
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

// newTestMgmtSvc creates a mgmtSvc that contains an IOServerInstance
// properly set up as an MS.
func newTestMgmtSvc(t *testing.T, log logging.Logger) *mgmtSvc {
	srv := newTestIOServer(log, true)

	harness := NewIOServerHarness(log)
	if err := harness.AddInstance(srv); err != nil {
		t.Fatal(err)
	}
	harness.started.SetTrue()

	ms, db := system.MockMembership(t, log, mockTCPResolver)
	return newMgmtSvc(harness, ms, db, nil, events.NewPubSub(context.Background(), log))
}

// newTestMgmtSvcMulti creates a mgmtSvc that contains the requested
// number of IOServerInstances. If requested, the first instance is
// configured as an access point.
func newTestMgmtSvcMulti(t *testing.T, log logging.Logger, count int, isAP bool) *mgmtSvc {
	harness := NewIOServerHarness(log)

	for i := 0; i < count; i++ {
		srv := newTestIOServer(log, i == 0 && isAP)
		srv._superblock.Rank = system.NewRankPtr(uint32(i))

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
	svc.sysdb = system.MockDatabaseWithAddr(t, log, nil)
	return svc
}
