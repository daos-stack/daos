//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"errors"
	"net"
	"testing"

	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

func TestAgentSecurityModule_ID(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, nil)

	test.AssertEqual(t, mod.ID(), drpc.ModuleSecurityAgent, "wrong drpc module")
}

func newTestSession(t *testing.T, log logging.Logger, conn net.Conn) *drpc.Session {
	svc := drpc.NewModuleService(log)
	return drpc.NewSession(conn, svc)
}

func defaultTestTransportConfig() *security.TransportConfig {
	return &security.TransportConfig{AllowInsecure: true}
}

func TestAgentSecurityModule_BadMethod(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, nil)
	method, err := mod.ID().GetMethod(-1)
	if method != nil {
		t.Errorf("Expected no method, got %+v", method)
	}

	test.CmpErr(t, errors.New("invalid method -1 for module Agent Security"), err)
}

func callRequestCreds(mod *SecurityModule, t *testing.T, log logging.Logger, conn net.Conn) ([]byte, error) {
	return mod.HandleCall(test.Context(t), newTestSession(t, log, conn), drpc.MethodRequestCredentials, nil)
}

func setupTestUnixConn(t *testing.T) (*net.UnixConn, func()) {
	conn := make(chan *net.UnixConn)
	path, lisCleanup := test.SetupTestListener(t, conn)

	client := getClientConn(t, path)

	newConn := <-conn

	cleanup := func() {
		client.Close()
		newConn.Close()
		lisCleanup()
	}

	return newConn, cleanup
}

func getClientConn(t *testing.T, path string) *drpc.ClientConnection {
	client := drpc.NewClientConnection(path)
	if err := client.Connect(test.Context(t)); err != nil {
		t.Fatalf("Failed to connect: %v", err)
	}
	return client
}

func expectCredResp(t *testing.T, respBytes []byte, expStatus int32, expCred bool) {
	if respBytes == nil {
		t.Error("Expected non-nil response")
	}

	resp := &auth.GetCredResp{}
	if err := proto.Unmarshal(respBytes, resp); err != nil {
		t.Fatalf("Couldn't unmarshal result: %v", err)
	}

	test.AssertEqual(t, resp.Status, expStatus, "status didn't match")

	test.AssertEqual(t, resp.Cred != nil, expCred, "credential expectation not met")
}

func TestAgentSecurityModule_RequestCreds_OK(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// Set up a real unix socket so we can make a real connection
	conn, cleanup := setupTestUnixConn(t)
	defer cleanup()

	mod := NewSecurityModule(log, defaultTestTransportConfig())
	mod.ext = auth.NewMockExtWithUser("agent-test", 0, 0)
	respBytes, err := callRequestCreds(mod, t, log, conn)

	if err != nil {
		t.Errorf("Expected no error, got %+v", err)
	}

	expectCredResp(t, respBytes, 0, true)
}

func TestAgentSecurityModule_RequestCreds_NotUnixConn(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, defaultTestTransportConfig())
	respBytes, err := callRequestCreds(mod, t, log, &net.TCPConn{})

	test.CmpErr(t, drpc.NewFailureWithMessage("connection is not a unix socket"), err)

	if respBytes != nil {
		t.Errorf("Expected no response, got: %v", respBytes)
	}
}

func TestAgentSecurityModule_RequestCreds_NotConnected(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// Set up a real unix socket so we can make a real connection
	conn, cleanup := setupTestUnixConn(t)
	defer cleanup()
	conn.Close() // can't get uid/gid from a closed connection

	mod := NewSecurityModule(log, defaultTestTransportConfig())
	respBytes, err := callRequestCreds(mod, t, log, conn)

	if err != nil {
		t.Errorf("Expected no error, got %+v", err)
	}

	expectCredResp(t, respBytes, int32(daos.MiscError), false)
}

func TestAgentSecurityModule_RequestCreds_BadConfig(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// Set up a real unix socket so we can make a real connection
	conn, cleanup := setupTestUnixConn(t)
	defer cleanup()

	// Empty TransportConfig is incomplete
	mod := NewSecurityModule(log, &security.TransportConfig{})
	respBytes, err := callRequestCreds(mod, t, log, conn)

	if err != nil {
		t.Errorf("Expected no error, got %+v", err)
	}

	expectCredResp(t, respBytes, int32(daos.BadCert), false)
}

func TestAgentSecurityModule_RequestCreds_BadUid(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// Set up a real unix socket so we can make a real connection
	conn, cleanup := setupTestUnixConn(t)
	defer cleanup()

	mod := NewSecurityModule(log, defaultTestTransportConfig())
	mod.ext = &auth.MockExt{
		LookupUserIDErr:  errors.New("LookupUserID"),
		LookupGroupIDErr: errors.New("LookupGroupID"),
	}
	respBytes, err := callRequestCreds(mod, t, log, conn)

	if err != nil {
		t.Errorf("Expected no error, got %+v", err)
	}

	expectCredResp(t, respBytes, int32(daos.MiscError), false)
}
