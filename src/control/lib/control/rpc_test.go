//
// (C) Copyright 2020-2021 Intel Corporation.
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
	"fmt"
	"math/rand"
	"os"
	"runtime"
	"sort"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

var defaultMessage = &MockMessage{}

type testRequest struct {
	retryableRequest
	rpcFn    unaryRPC
	toMS     bool
	HostList []string
	Deadline time.Time
}

func (tr *testRequest) isMSRequest() bool {
	return tr.toMS
}

func (tr *testRequest) SetHostList(hl []string) {
	tr.HostList = hl
}

func (tr *testRequest) getHostList() []string {
	return tr.HostList
}

func (tr *testRequest) SetTimeout(to time.Duration) {
	tr.Deadline = time.Now().Add(to)
}

func (tr *testRequest) getDeadline() time.Time {
	return tr.Deadline
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
		timeout    time.Duration
		withCancel *ctxCancel
		req        *testRequest
		expErr     error
		expResp    []*HostResponse
	}{
		"request timeout": {
			timeout: 1 * time.Nanosecond,
			req: &testRequest{
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
			if tc.timeout != 0 {
				tc.req.SetTimeout(tc.timeout)
			}

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
			if goRoutinesAtEnd > goRoutinesAtStart {
				t.Errorf("expected final goroutine count to be <= %d, got %d\n", goRoutinesAtStart, goRoutinesAtEnd)
				// Dump the stack to see which goroutines are lingering
				if err := unix.Kill(os.Getpid(), unix.SIGABRT); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}

func TestControl_InvokeUnaryRPC(t *testing.T) {
	// make the rand deterministic for testing
	msCandidateRandSource = rand.NewSource(1)

	clientCfg := DefaultConfig()
	clientCfg.TransportConfig.AllowInsecure = true
	clientCfg.HostList = nil
	for i := 0; i < maxMSCandidates*2; i++ {
		clientCfg.HostList = append(clientCfg.HostList, fmt.Sprintf("host%02d:%d", i, clientCfg.ControlPort))
	}

	leaderHost := "host08:10001"
	replicaHosts := []string{"host01:10001", "host05:10001", "host07:10001", "host08:10001", "host09:10001"}
	repMap := make(map[string]struct{})
	for _, rep := range replicaHosts {
		repMap[rep] = struct{}{}
	}
	nonLeaderReplicas := make([]string, 0, len(replicaHosts)-1)
	for _, rep := range replicaHosts {
		if rep != leaderHost {
			nonLeaderReplicas = append(nonLeaderReplicas, rep)
		}
	}

	var nonReplicaHosts []string
	for _, host := range clientCfg.HostList {
		if _, isRep := repMap[host]; !isRep {
			nonReplicaHosts = append(nonReplicaHosts, host)
		}
	}

	errNotLeader := &system.ErrNotLeader{
		LeaderHint: leaderHost,
		Replicas:   replicaHosts,
	}
	errNotLeaderNoLeader := &system.ErrNotLeader{
		Replicas: replicaHosts,
	}
	errNotReplica := &system.ErrNotReplica{
		Replicas: replicaHosts,
	}

	genRpcFn := func(inner func(*int) (proto.Message, error)) func(_ context.Context, _ *grpc.ClientConn) (proto.Message, error) {
		callCount := 0
		return func(_ context.Context, _ *grpc.ClientConn) (proto.Message, error) {
			return inner(&callCount)
		}
	}

	for name, tc := range map[string]struct {
		timeout    time.Duration
		withCancel *ctxCancel
		req        *testRequest
		expErr     error
		expResp    *UnaryResponse
	}{
		"request timeout": {
			timeout: 1 * time.Nanosecond,
			req: &testRequest{
				rpcFn: func(_ context.Context, _ *grpc.ClientConn) (proto.Message, error) {
					time.Sleep(1 * time.Microsecond)
					return defaultMessage, nil
				},
			},
			expErr: context.DeadlineExceeded,
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
		"multiple hosts in request, one fails": {
			req: &testRequest{
				retryableRequest: retryableRequest{
					retryTestFn: func(_ error, _ uint) bool { return false },
				},
				HostList: []string{"127.0.0.1:1", "127.0.0.1:2"},
				rpcFn: func(_ context.Context, cc *grpc.ClientConn) (proto.Message, error) {
					if cc.Target() == "127.0.0.1:1" {
						return nil, errors.New("whoops")
					}
					return defaultMessage, nil
				},
			},
			expResp: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr:  "127.0.0.1:1",
						Error: errors.New("whoops"),
					},
					{
						Addr:    "127.0.0.1:2",
						Message: defaultMessage,
					},
				},
			},
		},
		"request to starting leader retries successfully": {
			req: &testRequest{
				HostList: []string{leaderHost},
				toMS:     true,
				rpcFn: genRpcFn(func(callCount *int) (proto.Message, error) {
					*callCount++
					if *callCount == 1 {
						return nil, system.ErrRaftUnavail
					}
					return defaultMessage, nil
				}),
				retryableRequest: retryableRequest{
					// set a retry function that always returns false
					// to simulate a request with custom logic
					retryTestFn: func(_ error, _ uint) bool {
						return false
					},
				},
			},
			expResp: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr:    leaderHost,
						Message: defaultMessage,
					},
				},
			},
		},
		"request to non-leader replicas discovers leader": {
			req: &testRequest{
				HostList: nonLeaderReplicas,
				toMS:     true,
				rpcFn: func(_ context.Context, cc *grpc.ClientConn) (proto.Message, error) {
					if cc.Target() == errNotLeader.LeaderHint {
						return defaultMessage, nil
					}
					return nil, errNotLeader
				},
			},
			expResp: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr:    "host08:10001",
						Message: defaultMessage,
					},
				},
			},
		},
		"request to non-leader replicas with no current leader times out": {
			req: &testRequest{
				HostList: nonLeaderReplicas,
				Deadline: time.Now().Add(10 * time.Millisecond),
				toMS:     true,
				rpcFn: func(_ context.Context, cc *grpc.ClientConn) (proto.Message, error) {
					return nil, errNotLeaderNoLeader
				},
			},
			expErr: context.DeadlineExceeded,
		},
		"request to non-replicas eventually discovers at least one replica": {
			req: &testRequest{
				HostList: nonReplicaHosts,
				toMS:     true,
				rpcFn: func(_ context.Context, cc *grpc.ClientConn) (proto.Message, error) {
					if _, isRep := repMap[cc.Target()]; isRep {
						return defaultMessage, nil
					}
					return nil, errNotReplica
				},
			},
			expResp: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr:    "host01:10001",
						Message: defaultMessage,
					},
					{
						Addr:    "host05:10001",
						Message: defaultMessage,
					},
					{
						Addr:    "host07:10001",
						Message: defaultMessage,
					},
					{
						Addr:    "host08:10001",
						Message: defaultMessage,
					},
					{
						Addr:    "host09:10001",
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
			if tc.timeout != 0 {
				tc.req.SetTimeout(tc.timeout)
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
					cmp.Comparer(func(x, y error) bool { return common.CmpErrBool(x, y) }),
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
