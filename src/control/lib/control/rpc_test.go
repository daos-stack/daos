//
// (C) Copyright 2020 Intel Corporation.
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
//

package control

import (
	"context"
	"os"
	"runtime"
	"sort"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"golang.org/x/sys/unix"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

var defaultMessage = &MockMessage{}

type testRequest struct {
	rpcFn    unaryRPC
	toMS     bool
	HostList []string
	Timeout  time.Duration
}

func (tr *testRequest) isMSRequest() bool {
	return tr.toMS
}

func (tr *testRequest) getHostList() []string {
	return tr.HostList
}

func (tr *testRequest) getTimeout() time.Duration {
	return tr.Timeout
}

func (tr *testRequest) getRPC() unaryRPC {
	rpcFn := tr.rpcFn
	if rpcFn == nil {
		rpcFn = func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
			return defaultMessage, nil
		}
	}
	return rpcFn
}

type ctxCancel struct {
	ctx    context.Context
	cancel context.CancelFunc
}

func TestControl_InvokeUnaryRPCAsync(t *testing.T) {
	clientCfg := DefaultConfig()
	clientCfg.TransportConfig.AllowInsecure = true

	for name, tc := range map[string]struct {
		withCancel *ctxCancel
		req        *testRequest
		expErr     error
		expResp    []*HostResponse
	}{
		"request timeout": {
			req: &testRequest{
				Timeout: 1 * time.Nanosecond,
				rpcFn: func(_ context.Context, _ *grpc.ClientConn) (proto.Message, error) {
					time.Sleep(1 * time.Microsecond)
					return defaultMessage, nil
				},
			},
			expResp: []*HostResponse{
				{
					Addr:  clientCfg.HostList[0],
					Error: context.DeadlineExceeded,
				},
			},
		},
		"parent context canceled": {
			withCancel: func() *ctxCancel {
				ctx, cancel := context.WithCancel(context.Background())
				return &ctxCancel{
					ctx:    ctx,
					cancel: cancel,
				}
			}(),
			req: &testRequest{
				rpcFn: func(_ context.Context, _ *grpc.ClientConn) (proto.Message, error) {
					time.Sleep(10 * time.Second) // shouldn't be allowed to run this long
					return defaultMessage, nil
				},
			},
			expResp: []*HostResponse{{}},
		},
		"multiple hosts in request": {
			req: &testRequest{
				HostList: []string{"127.0.0.1:1", "127.0.0.1:2"},
			},
			expResp: []*HostResponse{
				{
					Addr:    "127.0.0.1:1",
					Message: defaultMessage,
				},
				{
					Addr:    "127.0.0.1:2",
					Message: defaultMessage,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			goRoutinesAtStart := runtime.NumGoroutine()

			client := NewClient(
				WithConfig(clientCfg),
				WithClientLogger(log),
			)

			outerCtx := context.TODO()
			if tc.withCancel != nil {
				outerCtx = tc.withCancel.ctx
			}
			ctx, cancel := context.WithCancel(outerCtx)
			defer cancel() // always clean up after test

			respChan, gotErr := client.InvokeUnaryRPCAsync(ctx, tc.req)
			if tc.withCancel != nil {
				// Cancel the parent context to test cleanup.
				tc.withCancel.cancel()
			}

			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// Don't bother with checking responses in the event that the parent
			// context was canceled -- this is a teardown and we don't care.
			if tc.withCancel == nil {
				gotResp := []*HostResponse{}
				for resp := range respChan {
					gotResp = append(gotResp, resp)
				}

				cmpOpts := []cmp.Option{
					cmp.Transformer("Sort", func(in []*HostResponse) []*HostResponse {
						out := append([]*HostResponse(nil), in...)
						sort.Slice(out, func(i, j int) bool { return out[i].Addr < out[j].Addr })
						return out
					}),
				}
				if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
					t.Fatalf("unexpected responses (-want, +got):\n%s\n", diff)
				}
			}

			// Explicitly clean up before checking for stragglers.
			cancel()
			// Give things a little bit of time to settle down before checking for
			// any lingering goroutines.
			time.Sleep(250 * time.Millisecond)
			goRoutinesAtEnd := runtime.NumGoroutine()
			if goRoutinesAtEnd != goRoutinesAtStart {
				t.Errorf("expected final goroutine count to be %d, got %d\n", goRoutinesAtStart, goRoutinesAtEnd)
				// Dump the stack to see which goroutines are lingering
				if err := unix.Kill(os.Getpid(), unix.SIGABRT); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}

func TestControl_InvokeUnaryRPC(t *testing.T) {
	clientCfg := DefaultConfig()
	clientCfg.TransportConfig.AllowInsecure = true

	for name, tc := range map[string]struct {
		withCancel *ctxCancel
		req        *testRequest
		expErr     error
		expResp    *UnaryResponse
	}{
		"request timeout": {
			req: &testRequest{
				Timeout: 1 * time.Nanosecond,
				rpcFn: func(_ context.Context, _ *grpc.ClientConn) (proto.Message, error) {
					time.Sleep(1 * time.Microsecond)
					return defaultMessage, nil
				},
			},
			expResp: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr:  clientCfg.HostList[0],
						Error: context.DeadlineExceeded,
					},
				},
			},
		},
		"parent context canceled": {
			withCancel: func() *ctxCancel {
				ctx, cancel := context.WithCancel(context.Background())
				return &ctxCancel{
					ctx:    ctx,
					cancel: cancel,
				}
			}(),
			req: &testRequest{
				rpcFn: func(_ context.Context, _ *grpc.ClientConn) (proto.Message, error) {
					time.Sleep(10 * time.Second) // shouldn't be allowed to run this long
					return defaultMessage, nil
				},
			},
			expErr: context.Canceled,
		},
		"multiple hosts in request": {
			req: &testRequest{
				HostList: []string{"127.0.0.1:1", "127.0.0.1:2"},
			},
			expResp: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr:    "127.0.0.1:1",
						Message: defaultMessage,
					},
					{
						Addr:    "127.0.0.1:2",
						Message: defaultMessage,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			client := NewClient(
				WithConfig(clientCfg),
				WithClientLogger(log),
			)

			outerCtx := context.TODO()
			if tc.withCancel != nil {
				outerCtx = tc.withCancel.ctx
			}
			ctx, cancel := context.WithCancel(outerCtx)
			defer cancel() // always clean up after test

			if tc.withCancel != nil {
				go func() {
					// Cancel the parent context to test cleanup.
					time.Sleep(1 * time.Millisecond)
					tc.withCancel.cancel()
				}()
			}
			gotResp, gotErr := client.InvokeUnaryRPC(ctx, tc.req)

			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// Don't bother with checking responses in the event that the parent
			// context was canceled -- this is a teardown and we don't care.
			if tc.withCancel == nil {
				cmpOpts := []cmp.Option{
					cmpopts.IgnoreUnexported(UnaryResponse{}),
					cmp.Transformer("Sort", func(in []*HostResponse) []*HostResponse {
						out := append([]*HostResponse(nil), in...)
						sort.Slice(out, func(i, j int) bool { return out[i].Addr < out[j].Addr })
						return out
					}),
				}
				if diff := cmp.Diff(tc.expResp, gotResp, cmpOpts...); diff != "" {
					t.Fatalf("unexpected responses (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}
