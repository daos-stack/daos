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

package drpc

import (
	"context"
	"os"
	"path/filepath"
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestNewSession(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	socket := newMockConn()
	svc := NewModuleService(log)
	s := NewSession(socket, svc)

	if s == nil {
		t.Fatal("session was nil!")
	}

	common.AssertEqual(t, s.Conn, socket, "UnixSocket wasn't set correctly")
	common.AssertEqual(t, s.mod, svc, "ModuleService wasn't set correctly")
}

func TestSession_ProcessIncomingMessage_ReadError(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	socket := newMockConn()
	socket.ReadOutputError = errors.New("mock read error")
	s := NewSession(socket, NewModuleService(log))

	err := s.ProcessIncomingMessage()

	common.CmpErr(t, socket.ReadOutputError, err)
}

func TestSession_ProcessIncomingMessage_WriteError(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	socket := newMockConn()
	socket.WriteOutputError = errors.New("mock write error")
	s := NewSession(socket, NewModuleService(log))

	err := s.ProcessIncomingMessage()

	common.CmpErr(t, socket.WriteOutputError, err)
}

func TestSession_ProcessIncomingMessage_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	socket := newMockConn()
	call := &Call{
		Sequence: 123,
		Module:   ModuleMgmt.ID(),
		Method:   MethodPoolCreate.ID(),
	}
	callBytes, err := proto.Marshal(call)
	if err != nil {
		t.Fatalf("failed to create test call: %v", err)
	}
	socket.ReadOutputBytes = callBytes
	socket.ReadOutputNumBytes = len(callBytes)

	mod := newTestModule(ModuleID(call.Module))
	svc := NewModuleService(log)
	svc.RegisterModule(mod)

	s := NewSession(socket, svc)

	if err = s.ProcessIncomingMessage(); err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}

	resp := &Response{}
	if err := proto.Unmarshal(socket.WriteInputBytes, resp); err != nil {
		t.Fatalf("bytes written to socket weren't a Response: %v", err)
	}

	expectedResp := &Response{
		Sequence: call.Sequence,
	}
	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}

func TestNewDomainSocketServer_NoSockFile(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	dss, err := NewDomainSocketServer(context.Background(), log, "")

	common.CmpErr(t, errors.New("Missing Argument"), err)
	common.AssertTrue(t, dss == nil, "expected no server created")
}

func TestNewDomainSocketServer(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expectedSock := "test.sock"

	dss, err := NewDomainSocketServer(context.Background(), log, expectedSock)

	if err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}

	if dss == nil {
		t.Fatal("expected server created, got nil")
	}

	common.AssertEqual(t, dss.sockFile, expectedSock, "wrong sockfile")
}

func TestServer_Start_CantUnlinkSocket(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	path := filepath.Join(tmpDir, "test.sock")

	// Forbid searching the directory
	if err := os.Chmod(tmpDir, 0000); err != nil {
		t.Fatalf("Couldn't change permissions on dir: %v", err)
	}
	defer func() {
		_ = os.Chmod(tmpDir, 0700)
	}()

	dss, _ := NewDomainSocketServer(context.Background(), log, path)

	err := dss.Start()

	common.CmpErr(t, errors.New("unlink"), err)
}

func TestServer_Start_CantListen(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	path := filepath.Join(tmpDir, "test.sock")

	// Forbid writing the directory
	if err := os.Chmod(tmpDir, 0500); err != nil {
		t.Fatalf("Couldn't change permissions on dir: %v", err)
	}
	defer func() {
		_ = os.Chmod(tmpDir, 0700)
	}()

	dss, _ := NewDomainSocketServer(context.Background(), log, path)

	err := dss.Start()

	common.CmpErr(t, errors.New("listen"), err)
}

func TestServer_RegisterModule(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := newTestModule(1234)
	dss, _ := NewDomainSocketServer(context.Background(), log, "dontcare.sock")

	dss.RegisterRPCModule(mod)

	result, ok := dss.service.GetModule(mod.ID())

	if !ok {
		t.Fatal("registered module not found in service")
	}

	if diff := cmp.Diff(mod, result); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}

func TestServer_Listen_AcceptError(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	lis := newMockListener()
	lis.acceptErr = errors.New("mock accept error")
	dss, _ := NewDomainSocketServer(context.Background(), log, "dontcare.sock")
	dss.listener = lis

	dss.Listen() // should return instantly

	common.AssertEqual(t, lis.acceptCallCount, 1, "should have returned after first error")
}

func TestServer_Listen_AcceptConnection(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	lis := newMockListener()
	lis.setNumConnsToAccept(3)
	dss, _ := NewDomainSocketServer(context.Background(), log, "dontcare.sock")
	dss.listener = lis

	dss.Listen() // will return when error is sent

	common.AssertEqual(t, lis.acceptCallCount, lis.acceptNumConns+1,
		"should have returned after listener errored")
	common.AssertEqual(t, len(dss.sessions), lis.acceptNumConns,
		"server should have made connections into sessions")
}

func TestServer_ListenSession_Error(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	dss, _ := NewDomainSocketServer(context.Background(), log, "dontcare.sock")
	conn := newMockConn()
	conn.ReadOutputError = errors.New("mock read error")
	session := NewSession(conn, dss.service)
	dss.sessions[conn] = session

	dss.listenSession(session) // will return when error is sent

	common.AssertEqual(t, conn.ReadCallCount, 1,
		"should have only hit the error once")
	common.AssertEqual(t, conn.CloseCallCount, 1, "should have closed connection")
	if _, ok := dss.sessions[conn]; ok {
		t.Fatal("session should have been removed but wasn't")
	}
}

func TestServer_Shutdown(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	dss, _ := NewDomainSocketServer(context.Background(), log, "dontcare.sock")
	lis := newCtxMockListener(dss.ctx)
	dss.listener = lis

	// Kick off the listen routine - it interacts with the ctx
	go dss.Listen()

	dss.Shutdown()

	_, ok := <-dss.ctx.Done()
	common.AssertFalse(t, ok, "expected context was canceled")

	// Wait for the mock listener to be closed
	<-lis.closed
}

// TestServer_IntegrationNoMethod verifies failure when adding a new
// module without specifying a method.
func TestServer_IntegrationNoMethod(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()
	path := filepath.Join(tmpDir, "test.sock")

	dss, _ := NewDomainSocketServer(context.Background(), log, path)

	// TEST module as defined in <daos/drpc_modules.h> has id 0
	mod := newTestModule(0)
	dss.RegisterRPCModule(mod)

	// Stand up a server loop
	if err := dss.Start(); err != nil {
		t.Fatalf("Couldn't start dRPC server: %v", err)
	}
	defer dss.Shutdown()

	// Now start a client...
	client := NewClientConnection(path)
	if err := client.Connect(); err != nil {
		t.Fatalf("failed to connect client: %v", err)
	}
	defer client.Close()

	call := &Call{
		Module: int32(mod.ID()),
	}
	resp, err := client.SendMsg(call)
	if err != nil {
		t.Fatalf("failed to send message: %v", err)
	}

	expectedResp := &Response{
		Sequence: call.Sequence,
		Status:   Status_UNKNOWN_METHOD,
	}
	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}
