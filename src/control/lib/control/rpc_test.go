//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"os"
	"runtime"
	"sort"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
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
	Timeout  time.Duration
	Sys      string
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
	tr.Timeout = to
	tr.Deadline = time.Now().Add(to)
}

func (tr *testRequest) getDeadline() time.Time {
	return tr.Deadline
}

func (tr *testRequest) getTimeout() time.Duration {
	return tr.Timeout
}

func (tr *testRequest) SetSystem(sys string) {
	tr.Sys = sys
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
			defer test.ShowBufferOnFailure(t, buf)

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

			test.CmpErr(t, tc.expErr, gotErr)
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

			deadliner, ok := (interface{})(t).(interface{ Deadline() (time.Time, bool) })
			if !ok {
				t.Log("go version < 1.15; skipping stragglers check")
				return
			}

			testDeadline, ok := deadliner.Deadline()
			if !ok {
				panic("no deadline")
			}
			// Set a deadline a bit before the overall test deadline so that we can
			// dump the stack if we have stuck goroutines.
			checkDeadline := testDeadline.Add(-1 * time.Second)

			// Explicitly clean up before checking for stragglers.
			cancel()

			// Give things a little bit of time to settle down before checking for
			// any lingering goroutines.
			time.Sleep(250 * time.Millisecond)
			for goRoutinesAtEnd := runtime.NumGoroutine(); goRoutinesAtEnd > goRoutinesAtStart; goRoutinesAtEnd = runtime.NumGoroutine() {
				time.Sleep(250 * time.Millisecond)
				t.Errorf("expected final goroutine count to be <= %d, got %d\n", goRoutinesAtStart, goRoutinesAtEnd)

				if time.Now().After(checkDeadline) {
					// Dump the stack to see which goroutines are lingering
					if err := unix.Kill(os.Getpid(), unix.SIGABRT); err != nil {
						t.Fatal(err)
					}
				}
			}
		})
	}
}

func TestControl_InvokeUnaryRPC(t *testing.T) {
	// make the rand deterministic for testing
	msCandidateRandSource = newSafeRandSource(1)

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
	errUnimplemented := status.Newf(codes.Unimplemented, "unimplemented").Err()

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
			expErr: errors.New("timed out"),
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
		"ctrl RPC handler unimplemented on one host": {
			req: &testRequest{
				HostList: []string{"127.0.0.1:1", "127.0.0.1:2"},
				rpcFn: func(_ context.Context, cc *grpc.ClientConn) (proto.Message, error) {
					if cc.Target() == "127.0.0.1:2" {
						return nil, errUnimplemented
					}
					return defaultMessage, nil
				},
			},
			expResp: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr:    "127.0.0.1:1",
						Message: defaultMessage,
					},
					{
						Addr:  "127.0.0.1:2",
						Error: errUnimplemented,
					},
				},
			},
		},
		"mgmt RPC handler unimplemented": {
			req: &testRequest{
				toMS: true,
				rpcFn: func(_ context.Context, _ *grpc.ClientConn) (proto.Message, error) {
					return nil, errUnimplemented
				},
			},
			expErr: errors.New("unimplemented"),
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
			expErr: FaultRpcTimeout(new(testRequest)),
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
			defer test.ShowBufferOnFailure(t, buf)

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

			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// Don't bother with checking responses in the event that the parent
			// context was canceled -- this is a teardown and we don't care.
			if tc.withCancel == nil {
				cmpOpts := []cmp.Option{
					cmpopts.IgnoreUnexported(UnaryResponse{}),
					cmp.Comparer(func(x, y error) bool { return test.CmpErrBool(x, y) }),
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
