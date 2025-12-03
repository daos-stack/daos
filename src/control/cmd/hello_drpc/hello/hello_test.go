//
// (C) Copyright 2025 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hello

import (
	"fmt"
	"path/filepath"
	"strings"
	"testing"

	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestDrpc_Hello_Integration(t *testing.T) {
	largeStrLen := drpc.MaxChunkSize + 1

	for name, tc := range map[string]struct {
		method      drpc.Method
		name        string
		expStatus   drpc.Status
		expGreeting string
	}{
		"bad method ID": {
			method:    helloMethod(-1),
			name:      "dontcare",
			expStatus: drpc.Status_UNKNOWN_METHOD,
		},
		"basic greeting": {
			method:      MethodGreeting,
			name:        "friend",
			expGreeting: "Hello friend",
		},
		"multi-chunk": {
			method:      MethodGreeting,
			name:        strings.Repeat("a", largeStrLen),
			expGreeting: fmt.Sprintf("Hello %s", strings.Repeat("a", largeStrLen)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			testDir, cleanupTestDir := test.CreateTestDir(t)
			defer cleanupTestDir()
			sockPath := filepath.Join(testDir, "test_drpc.sock")

			// dRPC server listens on the socket for client requests
			srv, err := drpc.NewDomainSocketServer(logging.FromContext(ctx), sockPath, 0640)
			if err != nil {
				t.Fatal(err)
			}
			srv.RegisterRPCModule(&HelloModule{})
			if err := srv.Start(ctx); err != nil {
				t.Fatal(err)
			}
			// NB: drpc server will be stopped when the test context is canceled

			// Message to be sent by the client to the server
			req := &Hello{
				Name: tc.name,
			}
			call := &drpc.Call{
				Module:   int32(Module_HELLO),
				Method:   tc.method.ID(),
				Sequence: 123,
			}
			if call.Body, err = proto.Marshal(req); err != nil {
				t.Fatal(err)
			}

			// Client will send a message to the server and receive a response
			client := drpc.NewClientConnection(sockPath)
			if err := client.Connect(ctx); err != nil {
				// Server is up - client should always be able to connect
				t.Fatal(err)
			}
			defer client.Close()

			resp, err := client.SendMsg(ctx, call)
			if err != nil {
				t.Fatal(err)
			}

			test.AssertEqual(t, tc.expStatus, resp.Status, fmt.Sprintf("unexpected response status: %s", resp.Status))
			test.AssertEqual(t, call.Sequence, resp.Sequence, "sequence numbers don't match")

			if resp.Status == drpc.Status_SUCCESS {
				// Attempt to unmarshal the payload
				var helloResp HelloResponse
				if err := proto.Unmarshal(resp.Body, &helloResp); err != nil {
					t.Fatal(err)
				}
				test.AssertEqual(t, tc.expGreeting, helloResp.Greeting, "")
			}
		})
	}
}
