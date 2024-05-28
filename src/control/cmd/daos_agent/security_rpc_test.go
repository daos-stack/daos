//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"errors"
	"net"
	"os/user"
	"syscall"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"golang.org/x/sys/unix"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/cache"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

func TestAgentSecurityModule_ID(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, defaultTestSecurityConfig())

	test.AssertEqual(t, mod.ID(), drpc.ModuleSecurityAgent, "wrong drpc module")
}

func newTestSession(t *testing.T, log logging.Logger, conn net.Conn) *drpc.Session {
	svc := drpc.NewModuleService(log)
	return drpc.NewSession(conn, svc)
}

func defaultTestSecurityConfig() *securityConfig {
	return &securityConfig{
		transport:   &security.TransportConfig{AllowInsecure: true},
		credentials: &security.CredentialConfig{},
	}
}

func TestAgentSecurityModule_BadMethod(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, defaultTestSecurityConfig())
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

func getClientConn(t *testing.T, path string) drpc.DomainSocketClient {
	t.Helper()
	client := drpc.NewClientConnection(path)
	if err := client.Connect(test.Context(t)); err != nil {
		t.Fatalf("Failed to connect: %v", err)
	}
	return client
}

func expectCredResp(t *testing.T, respBytes []byte, expStatus int32, expCred bool) {
	t.Helper()

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

	mod := NewSecurityModule(log, defaultTestSecurityConfig())
	respBytes, err := callRequestCreds(mod, t, log, conn)

	if err != nil {
		t.Errorf("Expected no error, got %+v", err)
	}

	expectCredResp(t, respBytes, 0, true)
}

func TestAgentSecurityModule_RequestCreds_NotUnixConn(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, defaultTestSecurityConfig())
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

	mod := NewSecurityModule(log, defaultTestSecurityConfig())
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
	mod := NewSecurityModule(log, &securityConfig{
		transport:   &security.TransportConfig{},
		credentials: &security.CredentialConfig{},
	})
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

	mod := NewSecurityModule(log, defaultTestSecurityConfig())
	mod.signCredential = func(_ context.Context, _ *auth.CredentialRequest) (*auth.Credential, error) {
		return nil, errors.New("LookupUserID")
	}
	respBytes, err := callRequestCreds(mod, t, log, conn)

	if err != nil {
		t.Errorf("Expected no error, got %+v", err)
	}

	expectCredResp(t, respBytes, int32(daos.MiscError), false)
}

type signCredentialResp struct {
	cred *auth.Credential
	err  error
}

func TestAgent_SecurityRPC_getCredential(t *testing.T) {
	testCred := &auth.Credential{
		Token:  &auth.Token{Flavor: auth.Flavor_AUTH_SYS, Data: []byte("test-token")},
		Origin: "test-origin",
	}
	miscErrBytes, err := proto.Marshal(
		&auth.GetCredResp{
			Status: int32(daos.MiscError),
		},
	)
	if err != nil {
		t.Fatalf("Couldn't marshal misc error: %v", err)
	}
	successBytes, err := proto.Marshal(
		&auth.GetCredResp{
			Status: 0,
			Cred:   testCred,
		},
	)
	if err != nil {
		t.Fatalf("Couldn't marshal success: %v", err)
	}

	for name, tc := range map[string]struct {
		secCfg    *securityConfig
		responses []signCredentialResp
		expBytes  []byte
		expErr    error
	}{
		"lookup miss": {
			secCfg: defaultTestSecurityConfig(),
			responses: []signCredentialResp{
				{
					cred: nil,
					err:  user.UnknownUserIdError(unix.Getuid()),
				},
			},
			expBytes: miscErrBytes,
		},
		"lookup OK": {
			secCfg: func() *securityConfig {
				cfg := defaultTestSecurityConfig()
				cfg.credentials.ClientUserMap = security.ClientUserMap{
					uint32(unix.Getuid()): &security.MappedClientUser{
						User: "test-user",
					},
				}
				return cfg
			}(),
			responses: []signCredentialResp{
				{
					cred: nil,
					err:  user.UnknownUserIdError(unix.Getuid()),
				},
				{
					cred: testCred,
					err:  nil,
				},
			},
			expBytes: successBytes,
		},
		"lookup OK, but retried request fails": {
			secCfg: func() *securityConfig {
				cfg := defaultTestSecurityConfig()
				cfg.credentials.ClientUserMap = security.ClientUserMap{
					uint32(unix.Getuid()): &security.MappedClientUser{
						User: "test-user",
					},
				}
				return cfg
			}(),
			responses: []signCredentialResp{
				{
					cred: nil,
					err:  user.UnknownUserIdError(unix.Getuid()),
				},
				{
					cred: nil,
					err:  errors.New("oops"),
				},
			},
			expBytes: miscErrBytes,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			// Set up a real unix socket so we can make a real connection
			conn, cleanup := setupTestUnixConn(t)
			defer cleanup()

			mod := NewSecurityModule(log, tc.secCfg)
			mod.signCredential = func() func(_ context.Context, req *auth.CredentialRequest) (*auth.Credential, error) {
				var idx int
				return func(_ context.Context, req *auth.CredentialRequest) (*auth.Credential, error) {
					defer func() {
						if idx < len(tc.responses)-1 {
							idx++
						}
					}()
					t.Logf("returning response %d: %+v", idx, tc.responses[idx])
					return tc.responses[idx].cred, tc.responses[idx].err
				}
			}()

			respBytes, gotErr := callRequestCreds(mod, t, log, conn)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expBytes, respBytes); diff != "" {
				t.Errorf("unexpected response (-want +got):\n%s", diff)
			}
		})
	}
}

func TestAgent_SecurityCachedCredentials(t *testing.T) {
	cred0 := &auth.Credential{
		Token:  &auth.Token{Flavor: auth.Flavor_AUTH_SYS, Data: []byte("user,group1,group2")},
		Origin: "test-origin",
	}
	cred1 := &auth.Credential{
		Token:  &auth.Token{Flavor: auth.Flavor_AUTH_SYS, Data: []byte("user,group1,group3")},
		Origin: "test-origin",
	}

	for name, tc := range map[string]struct {
		lifetime  time.Duration
		req       *auth.CredentialRequest
		responses []signCredentialResp
		exp       *auth.Credential
	}{
		"cache hit": {
			lifetime: time.Second,
			req: &auth.CredentialRequest{
				DomainInfo: security.InitDomainInfo(&syscall.Ucred{Uid: 1234, Gid: 5678}, ""),
			},
			responses: []signCredentialResp{
				{
					cred: cred0,
					err:  nil,
				},
				{
					cred: cred1,
					err:  nil,
				},
			},
			exp: cred0,
		},
		"expired entry": {
			lifetime: time.Nanosecond,
			req: &auth.CredentialRequest{
				DomainInfo: security.InitDomainInfo(&syscall.Ucred{Uid: 1234, Gid: 5678}, ""),
			},
			responses: []signCredentialResp{
				{
					cred: cred0,
					err:  nil,
				},
				{
					cred: cred1,
					err:  nil,
				},
			},
			exp: cred1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cache := &credentialCache{
				log:          log,
				cache:        cache.NewItemCache(log),
				credLifetime: tc.lifetime,
				cacheMissFn: func() func(_ context.Context, req *auth.CredentialRequest) (*auth.Credential, error) {
					var idx int
					return func(_ context.Context, req *auth.CredentialRequest) (*auth.Credential, error) {
						defer func() {
							if idx < len(tc.responses)-1 {
								idx++
							}
						}()
						t.Logf("returning response %d: %+v", idx, tc.responses[idx])
						return tc.responses[idx].cred, tc.responses[idx].err
					}
				}(),
			}

			// Prime the cache with a single entry.
			_, err := cache.getSignedCredential(test.Context(t), tc.req)
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			// Request a second time with the same credentials.
			cred, err := cache.getSignedCredential(test.Context(t), tc.req)
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			cmpOpts := cmp.Options{
				protocmp.Transform(),
			}
			if diff := cmp.Diff(tc.exp, cred, cmpOpts...); diff != "" {
				t.Errorf("unexpected credential (-want +got):\n%s", diff)
			}
		})
	}
}
