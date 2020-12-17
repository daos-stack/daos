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
//

package server

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	testShortTimeout   = 50 * time.Millisecond
	testLongTimeout    = 1 * time.Minute
	delayedFailTimeout = 20 * testShortTimeout
	maxIOServers       = 2
)

func TestServer_Harness_Start(t *testing.T) {
	for name, tc := range map[string]struct {
		trc              *ioserver.TestRunnerConfig
		isAP             bool                     // is first instance an AP/MS replica/bootstrap
		rankInSuperblock bool                     // rank already set in superblock when starting
		instanceUuids    map[int]string           // UUIDs for each instance.Index()
		dontNotifyReady  bool                     // skip sending notify ready on dRPC channel
		waitTimeout      time.Duration            // time after which test context is cancelled
		expStartErr      error                    // error from harness.Start()
		expStartCount    uint32                   // number of instance.runner.Start() calls
		expDrpcCalls     map[uint32][]drpc.Method // method ids called for each instance.Index()
		expGrpcCalls     map[uint32][]string      // string repr of call for each instance.Index()
		expRanks         map[uint32]system.Rank   // ranks to have been set during Start()
		expMembers       system.Members           // members to have been registered during Start()
		expIoErrs        map[uint32]error         // errors expected from instances
	}{
		"normal startup/shutdown": {
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(testLongTimeout)
					return errors.New("ending")
				},
			},
			instanceUuids: map[int]string{
				0: MockUUID(0),
				1: MockUUID(1),
			},
			expStartCount: maxIOServers,
			expDrpcCalls: map[uint32][]drpc.Method{
				0: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
				1: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
			},
			expGrpcCalls: map[uint32][]string{
				0: {fmt.Sprintf("Join %d", system.NilRank)},
				1: {fmt.Sprintf("Join %d", system.NilRank)},
			},
			expRanks: map[uint32]system.Rank{
				0: system.Rank(0),
				1: system.Rank(1),
			},
		},
		"startup/shutdown with preset ranks": {
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(testLongTimeout)
					return errors.New("ending")
				},
			},
			rankInSuperblock: true,
			expStartCount:    maxIOServers,
			expDrpcCalls: map[uint32][]drpc.Method{
				0: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
				1: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
			},
			expGrpcCalls: map[uint32][]string{
				0: {"Join 1"}, // rank == instance.Index() + 1
				1: {"Join 2"},
			},
			expRanks: map[uint32]system.Rank{
				0: system.Rank(1),
				1: system.Rank(2),
			},
		},
		"fails to start": {
			trc:           &ioserver.TestRunnerConfig{StartErr: errors.New("no")},
			waitTimeout:   10 * testShortTimeout,
			expStartErr:   context.DeadlineExceeded,
			expStartCount: 2, // both start but don't proceed so context times out
		},
		"delayed failure occurs before notify ready": {
			dontNotifyReady: true,
			waitTimeout:     30 * testShortTimeout,
			expStartErr:     context.DeadlineExceeded,
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(delayedFailTimeout)
					return errors.New("oops")
				},
			},
			expStartCount: maxIOServers,
			expRanks: map[uint32]system.Rank{
				0: system.NilRank,
				1: system.NilRank,
			},
			expIoErrs: map[uint32]error{
				0: errors.New("oops"),
				1: errors.New("oops"),
			},
		},
		"delayed failure occurs after ready": {
			waitTimeout: 100 * testShortTimeout,
			expStartErr: context.DeadlineExceeded,
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(delayedFailTimeout)
					return errors.New("oops")
				},
			},
			instanceUuids: map[int]string{
				0: MockUUID(0),
				1: MockUUID(1),
			},
			expStartCount: maxIOServers,
			expDrpcCalls: map[uint32][]drpc.Method{
				0: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
				1: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
			},
			expGrpcCalls: map[uint32][]string{
				0: {fmt.Sprintf("Join %d", system.NilRank)},
				1: {fmt.Sprintf("Join %d", system.NilRank)},
			},
			expRanks: map[uint32]system.Rank{
				0: system.Rank(0),
				1: system.Rank(1),
			},
			expIoErrs: map[uint32]error{
				0: errors.New("oops"),
				1: errors.New("oops"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			testDir, cleanup := CreateTestDir(t)
			defer cleanup()

			srvCfgs := make([]*ioserver.Config, maxIOServers)
			for i := 0; i < maxIOServers; i++ {
				srvCfgs[i] = ioserver.NewConfig().
					WithScmClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint(filepath.Join(testDir, strconv.Itoa(i)))
			}
			config := config.DefaultServer().
				WithServers(srvCfgs...).
				WithSocketDir(testDir).
				WithTransportConfig(&security.TransportConfig{AllowInsecure: true})

			joinMu := sync.Mutex{}
			joinRequests := make(map[uint32][]string)
			var instanceStarts uint32
			harness := NewIOServerHarness(log)
			for i, srvCfg := range config.Servers {
				if err := os.MkdirAll(srvCfg.Storage.SCM.MountPoint, 0777); err != nil {
					t.Fatal(err)
				}

				if tc.trc == nil {
					tc.trc = &ioserver.TestRunnerConfig{}
				}
				if tc.trc.StartCb == nil {
					tc.trc.StartCb = func() {
						atomic.StoreUint32(&instanceStarts,
							atomic.AddUint32(&instanceStarts, 1))
					}
				}
				runner := ioserver.NewTestRunner(tc.trc, srvCfg)
				bdevProvider, err := bdev.NewClassProvider(log,
					srvCfg.Storage.SCM.MountPoint, &srvCfg.Storage.Bdev)
				if err != nil {
					t.Fatal(err)
				}
				scmProvider := scm.NewMockProvider(log, nil, &scm.MockSysConfig{IsMountedBool: true})

				idx := uint32(i)
				joinFn := func(_ context.Context, req *control.SystemJoinReq) (*control.SystemJoinResp, error) {
					// appease the race detector
					joinMu.Lock()
					defer joinMu.Unlock()
					joinRequests[idx] = []string{fmt.Sprintf("Join %d", req.Rank)}
					return &control.SystemJoinResp{
						Rank: system.Rank(idx),
					}, nil
				}

				srv := NewIOServerInstance(log, bdevProvider, scmProvider, joinFn, runner)
				var isAP bool
				if tc.isAP && i == 0 { // first instance will be AP & bootstrap MS
					isAP = true
				}
				var uuid string
				if UUID, exists := tc.instanceUuids[i]; exists {
					uuid = UUID
				}
				var rank *system.Rank
				var isValid bool
				if tc.rankInSuperblock {
					rank = system.NewRankPtr(uint32(i + 1))
					isValid = true
				} else if isAP { // bootstrap will assume rank 0
					rank = new(system.Rank)
				}
				srv.setSuperblock(&Superblock{
					UUID: uuid, Rank: rank, ValidRank: isValid,
				})

				if err := harness.AddInstance(srv); err != nil {
					t.Fatal(err)
				}
			}

			instances := harness.Instances()

			// set mock dRPC client to record call details
			for _, srv := range instances {
				srv.setDrpcClient(newMockDrpcClient(&mockDrpcClientConfig{
					SendMsgResponse: &drpc.Response{},
				}))
			}

			ctx, cancel := context.WithCancel(context.Background())
			if tc.waitTimeout != 0 {
				ctx, cancel = context.WithTimeout(ctx, tc.waitTimeout)
			}
			defer cancel()

			// start harness async and signal completion
			var gotErr error
			membership, sysdb := system.MockMembership(t, log, mockTCPResolver)
			done := make(chan struct{})
			go func(ctxIn context.Context) {
				gotErr = harness.Start(ctxIn, sysdb, config)
				close(done)
			}(ctx)

			waitDrpcReady := make(chan struct{})
			t.Log("waiting for dRPC to be ready")
			go func(ctxIn context.Context) {
				for {
					ready := true
					for _, srv := range instances {
						if srv.waitDrpc.IsFalse() {
							ready = false
						}
					}
					if ready {
						close(waitDrpcReady)
						return
					}
					select {
					case <-time.After(testShortTimeout):
					case <-ctxIn.Done():
						return
					}
				}
			}(ctx)

			select {
			case <-waitDrpcReady:
				t.Log("dRPC is ready")
			case <-ctx.Done():
				if tc.expStartErr != nil {
					<-done
					CmpErr(t, tc.expStartErr, gotErr)
					if atomic.LoadUint32(&instanceStarts) != tc.expStartCount {
						t.Fatalf("expected %d starts, got %d",
							tc.expStartCount, instanceStarts)
					}
					return
				}
				// deadline exceeded as expected but desired state not reached
				t.Fatalf("instances did not get to waiting for dRPC state: %s", ctx.Err())
			}
			t.Log("instances ready and waiting for dRPC ready notification")

			// simulate receiving notify ready whilst instances
			// running in harness (unless dontNotifyReady flag is set)
			for _, srv := range instances {
				if tc.dontNotifyReady {
					continue
				}
				req := getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0)
				go func(ctxIn context.Context, i *IOServerInstance) {
					select {
					case i.drpcReady <- req:
					case <-ctxIn.Done():
					}
				}(ctx, srv)
				t.Logf("sent drpc ready to instance %d", srv.Index())
			}

			waitReady := make(chan struct{})
			t.Log("waitng for ready")
			go func(ctxIn context.Context) {
				for {
					if len(harness.readyRanks()) == len(instances) {
						close(waitReady)
						return
					}
					select {
					case <-time.After(testShortTimeout):
					case <-ctxIn.Done():
						return
					}
				}
			}(ctx)

			select {
			case <-waitReady:
				t.Log("instances setup and ready")
			case <-ctx.Done():
				t.Logf("instances did not get to ready state (%s)", ctx.Err())
			}

			if atomic.LoadUint32(&instanceStarts) != tc.expStartCount {
				t.Fatalf("expected %d starts, got %d", tc.expStartCount, instanceStarts)
			}

			if tc.waitTimeout == 0 { // if custom timeout, don't cancel
				cancel() // all ranks have been started, run finished
			}
			<-done
			if gotErr != context.Canceled || tc.expStartErr != nil {
				CmpErr(t, tc.expStartErr, gotErr)
				if tc.expStartErr != nil {
					return
				}
			}

			joinMu.Lock()
			defer joinMu.Unlock()
			// verify expected RPCs were made, ranks allocated and
			// members added to membership
			for _, srv := range instances {
				dc, err := srv.getDrpcClient()
				if err != nil {
					t.Fatal(err)
				}
				gotDrpcCalls := dc.(*mockDrpcClient).CalledMethods()
				AssertEqual(t, tc.expDrpcCalls[srv.Index()], gotDrpcCalls,
					fmt.Sprintf("%s: unexpected dRPCs for instance %d", name, srv.Index()))

				if diff := cmp.Diff(tc.expGrpcCalls[srv.Index()], joinRequests[srv.Index()]); diff != "" {
					t.Fatalf("unexpected gRPCs for instance %d (-want, +got):\n%s\n",
						srv.Index(), diff)
				}
				rank, _ := srv.GetRank()
				if diff := cmp.Diff(tc.expRanks[srv.Index()], rank); diff != "" {
					t.Fatalf("unexpected rank for instance %d (-want, +got):\n%s\n",
						srv.Index(), diff)
				}
				CmpErr(t, tc.expIoErrs[srv.Index()], srv._lastErr)
			}
			members := membership.Members(nil)
			AssertEqual(t, len(tc.expMembers), len(members), "unexpected number in membership")
			for i, member := range members {
				if diff := cmp.Diff(fmt.Sprintf("%v", member),
					fmt.Sprintf("%v", tc.expMembers[i])); diff != "" {

					t.Fatalf("unexpected system membership (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestServer_Harness_WithFaultDomain(t *testing.T) {
	harness := &IOServerHarness{}
	fd, err := system.NewFaultDomainFromString("/one/two")
	if err != nil {
		t.Fatalf("couldn't create fault domain: %s", err)
	}

	updatedHarness := harness.WithFaultDomain(fd)

	// Updated to include the fault domain
	if diff := cmp.Diff(harness.faultDomain, fd); diff != "" {
		t.Fatalf("unexpected results (-want, +got):\n%s\n", diff)
	}
	// updatedHarness is the same as harness
	AssertEqual(t, updatedHarness, harness, "not the same structure")
}
