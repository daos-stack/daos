//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"context"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/logging"
)

var testFileMode os.FileMode = 0600

func TestNewSession(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	socket := newMockConn()
	svc := NewModuleService(log)
	s := NewSession(socket, svc)

	if s == nil {
		t.Fatal("session was nil!")
	}

	test.AssertEqual(t, s.Conn, socket, "UnixSocket wasn't set correctly")
	test.AssertEqual(t, s.mod, svc, "ModuleService wasn't set correctly")
}

func TestSession_ProcessIncomingMessage_ReadError(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	socket := newMockConn()
	socket.ReadOutputError = errors.New("mock read error")
	s := NewSession(socket, NewModuleService(log))

	err := s.ProcessIncomingMessage(test.Context(t))

	test.CmpErr(t, socket.ReadOutputError, err)
}

func TestSession_ProcessIncomingMessage_WriteError(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	socket := newMockConn()
	socket.WriteOutputError = errors.New("mock write error")
	s := NewSession(socket, NewModuleService(log))

	err := s.ProcessIncomingMessage(test.Context(t))

	test.CmpErr(t, socket.WriteOutputError, err)
}

func TestSession_ProcessIncomingMessage_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

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

	if err = s.ProcessIncomingMessage(test.Context(t)); err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}

	resp := &Response{}
	if err := proto.Unmarshal(socket.WriteInputBytes, resp); err != nil {
		t.Fatalf("bytes written to socket weren't a Response: %v", err)
	}

	expectedResp := &Response{
		Sequence: call.Sequence,
	}
	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}

func TestNewDomainSocketServer_NoSockFile(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dss, err := NewDomainSocketServer(log, "", testFileMode)

	test.CmpErr(t, errors.New("Missing Argument"), err)
	test.AssertTrue(t, dss == nil, "expected no server created")
}

func TestNewDomainSocketServer_NoSockFileMode(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dss, err := NewDomainSocketServer(log, "test.sock", 0)

	test.CmpErr(t, errors.New("Missing Argument"), err)
	test.AssertTrue(t, dss == nil, "expected no server created")
}

func TestNewDomainSocketServer(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	expectedSock := "test.sock"

	dss, err := NewDomainSocketServer(log, expectedSock, testFileMode)

	if err != nil {
		t.Fatalf("expected no error, got: %v", err)
	}

	if dss == nil {
		t.Fatal("expected server created, got nil")
	}

	test.AssertEqual(t, dss.sockFile, expectedSock, "wrong sockfile")
}

func TestDrpc_DomainSocketServer_Start(t *testing.T) {
	sockPath := func(dir string) string {
		return filepath.Join(dir, "test.sock")
	}

	for name, tc := range map[string]struct {
		nilServer bool
		setup     func(t *testing.T, dir string) func()
		expErr    error
	}{
		"nil": {
			nilServer: true,
			expErr:    errors.New("nil"),
		},
		"unused existing socket file": {
			setup: func(t *testing.T, dir string) func() {
				t.Helper()

				f, err := os.Create(sockPath(dir))
				if err != nil {
					t.Fatal(err)
				}
				_ = f.Close()
				return func() {}
			},
		},
		"can't unlink old socket file": {
			setup: func(t *testing.T, dir string) func() {
				t.Helper()

				sockFile := sockPath(dir)
				f, err := os.Create(sockFile)
				if err != nil {
					t.Fatal(err)
				}
				_ = f.Close()

				if err := os.Chmod(dir, 0500); err != nil {
					t.Fatalf("Couldn't change permissions on dir: %v", err)
				}
				return func() {
					_ = os.Chmod(dir, 0700)
				}
			},
			expErr: errors.New("unlink"),
		},
		"socket file in use": {
			setup: func(t *testing.T, dir string) func() {
				t.Helper()
				log, buf := logging.NewTestLogger(t.Name())
				defer test.ShowBufferOnFailure(t, buf)

				other, err := NewDomainSocketServer(log, sockPath(dir), testFileMode)
				if err != nil {
					t.Fatalf("can't create first server: %s", err.Error())
				}

				err = other.Start(test.Context(t))
				if err != nil {
					t.Fatalf("can't start up first server: %s", err.Error())
				}

				// NB: The started server is shut down when the test context is canceled.
				return func() {}
			},
			expErr: FaultSocketFileInUse(""),
		},
		"listen fails": {
			setup: func(t *testing.T, dir string) func() {
				t.Helper()

				if err := os.Chmod(dir, 0500); err != nil {
					t.Fatalf("Couldn't change permissions on dir: %v", err)
				}
				return func() {
					_ = os.Chmod(dir, 0700)
				}
			},
			expErr: errors.New("listen"),
		},
		"success": {},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			tmpDir, tmpCleanup := test.CreateTestDir(t)
			defer tmpCleanup()

			if tc.setup != nil {
				teardown := tc.setup(t, tmpDir)
				defer teardown()
			}

			// Test hack - make sure the right path is included in the fault message for comparison
			if fault.IsFaultCode(tc.expErr, code.SocketFileInUse) {
				tc.expErr = FaultSocketFileInUse(sockPath(tmpDir))
			}

			var err error
			var dss *DomainSocketServer
			if !tc.nilServer {
				dss, err = NewDomainSocketServer(log, sockPath(tmpDir), testFileMode)
				if err != nil {
					t.Fatal(err)
				}
			}

			err = dss.Start(test.Context(t))

			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestServer_RegisterModule(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := newTestModule(1234)
	dss, _ := NewDomainSocketServer(log, "dontcare.sock", testFileMode)

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
	defer test.ShowBufferOnFailure(t, buf)

	lis := newMockListener()
	lis.acceptErr = errors.New("mock accept error")
	dss, _ := NewDomainSocketServer(log, "dontcare.sock", testFileMode)
	dss.listener = lis

	dss.Listen(test.Context(t)) // should return instantly

	test.AssertEqual(t, lis.acceptCallCount, 1, "should have returned after first error")
}

func TestServer_Listen_AcceptConnection(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	lis := newMockListener()
	lis.setNumConnsToAccept(3)
	dss, _ := NewDomainSocketServer(log, "dontcare.sock", testFileMode)
	dss.listener = lis

	dss.Listen(test.Context(t)) // will return when error is sent

	test.AssertEqual(t, lis.acceptCallCount, lis.acceptNumConns+1,
		"should have returned after listener errored")
	test.AssertEqual(t, len(dss.sessions), lis.acceptNumConns,
		"server should have made connections into sessions")
}

func TestServer_ListenSession_Error(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	dss, _ := NewDomainSocketServer(log, "dontcare.sock", testFileMode)
	conn := newMockConn()
	conn.ReadOutputError = errors.New("mock read error")
	session := NewSession(conn, dss.service)
	dss.sessions[conn] = session

	dss.listenSession(test.Context(t), session) // will return when error is sent

	test.AssertEqual(t, conn.ReadCallCount, 1,
		"should have only hit the error once")
	test.AssertEqual(t, conn.CloseCallCount, 1, "should have closed connection")
	if _, ok := dss.sessions[conn]; ok {
		t.Fatal("session should have been removed but wasn't")
	}
}

func TestServer_Shutdown(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, shutdown := context.WithCancel(test.Context(t))

	dss, _ := NewDomainSocketServer(log, "dontcare.sock", testFileMode)
	lis := newCtxMockListener(ctx)
	dss.listener = lis

	// Kick off the listen routine - it interacts with the ctx
	go dss.Listen(ctx)

	shutdown()

	_, ok := <-ctx.Done()
	test.AssertFalse(t, ok, "expected context was canceled")

	// Wait for the mock listener to be closed
	<-lis.closed
}

// TestServer_IntegrationNoMethod verifies failure when adding a new
// module without specifying a method.
func TestServer_IntegrationNoMethod(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := test.CreateTestDir(t)
	defer tmpCleanup()
	path := filepath.Join(tmpDir, "test.sock")

	dss, err := NewDomainSocketServer(log, path, testFileMode)
	if err != nil {
		t.Fatal(err)
	}

	// TEST module as defined in <daos/drpc_modules.h> has id 0
	mod := newTestModule(0)
	dss.RegisterRPCModule(mod)

	ctx, shutdown := context.WithCancel(test.Context(t))
	defer shutdown()

	// Stand up a server loop
	if err := dss.Start(ctx); err != nil {
		t.Fatalf("Couldn't start dRPC server: %v", err)
	}

	// Now start a client...
	client := NewClientConnection(path)
	if err := client.Connect(context.Background()); err != nil {
		t.Fatalf("failed to connect client: %v", err)
	}
	defer client.Close()

	call := &Call{
		Module: int32(mod.ID()),
	}
	resp, err := client.SendMsg(test.Context(t), call)
	if err != nil {
		t.Fatalf("failed to send message: %v", err)
	}

	expectedResp := &Response{
		Sequence: call.Sequence,
		Status:   Status_UNKNOWN_METHOD,
	}
	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(expectedResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}
