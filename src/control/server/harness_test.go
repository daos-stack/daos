//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	. "github.com/daos-stack/daos/src/control/common"

	"github.com/daos-stack/daos/src/control/system"
)

const (
	testShortTimeout   = 50 * time.Millisecond
	testLongTimeout    = 1 * time.Minute
	delayedFailTimeout = 20 * testShortTimeout
	maxEngines         = 2
)

// TODO: FIX ASAP; critical functionality tested here
//
/*func TestServer_Harness_Start(t *testing.T) {
	for name, tc := range map[string]struct {
		trc              *engine.TestRunnerConfig
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
			trc: &engine.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(testLongTimeout)
					return errors.New("ending")
				},
			},
			instanceUuids: map[int]string{
				0: MockUUID(0),
				1: MockUUID(1),
			},
			expStartCount: maxEngines,
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
			trc: &engine.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(testLongTimeout)
					return errors.New("ending")
				},
			},
			rankInSuperblock: true,
			expStartCount:    maxEngines,
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
			trc:           &engine.TestRunnerConfig{StartErr: errors.New("no")},
			waitTimeout:   10 * testShortTimeout,
			expStartErr:   context.DeadlineExceeded,
			expStartCount: 2, // both start but don't proceed so context times out
		},
		"delayed failure occurs before notify ready": {
			dontNotifyReady: true,
			waitTimeout:     30 * testShortTimeout,
			expStartErr:     context.DeadlineExceeded,
			trc: &engine.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(delayedFailTimeout)
					return errors.New("oops")
				},
			},
			expStartCount: maxEngines,
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
			trc: &engine.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(delayedFailTimeout)
					return errors.New("oops")
				},
			},
			instanceUuids: map[int]string{
				0: MockUUID(0),
				1: MockUUID(1),
			},
			expStartCount: maxEngines,
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

			engineCfgs := make([]*engine.Config, maxEngines)
			for i := 0; i < maxEngines; i++ {
				engineCfgs[i] = engine.NewConfig().
					WithStorage(
						storage.NewTierConfig().
							WithScmClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint(filepath.Join(testDir, strconv.Itoa(i))),
					)
			}
			config := config.DefaultServer().
				WithEngines(engineCfgs...).
				WithSocketDir(testDir).
				WithTransportConfig(&security.TransportConfig{AllowInsecure: true})

			joinMu := sync.Mutex{}
			joinRequests := make(map[uint32][]string)
			var instanceStarts uint32
			harness := NewEngineHarness(log)
			for i, engineCfg := range config.Engines {
				if err := os.MkdirAll(engineCfg.Storage.Tiers[0].Scm.MountPoint, 0777); err != nil {
					t.Fatal(err)
				}

				if tc.trc == nil {
					tc.trc = &engine.TestRunnerConfig{}
				}
				if tc.trc.StartCb == nil {
					tc.trc.StartCb = func() {
						atomic.StoreUint32(&instanceStarts,
							atomic.AddUint32(&instanceStarts, 1))
					}
				}
				runner := engine.NewTestRunner(tc.trc, engineCfg)
				provider := storage.DefaultProvider(log, i, &engineCfg.Storage)

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

				ei := NewEngineInstance(log, provider, joinFn, runner)
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
				ei.setSuperblock(&Superblock{
					UUID: uuid, Rank: rank, ValidRank: isValid,
				})

				if err := harness.AddInstance(ei); err != nil {
					t.Fatal(err)
				}
			}

			instances := harness.Instances()

			// set mock dRPC client to record call details
			for _, ei := range instances {
				ei.setDrpcClient(newMockDrpcClient(&mockDrpcClientConfig{
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
				gotErr = harness.Start(ctxIn, sysdb, nil, config)
				close(done)
			}(ctx)

			waitDrpcReady := make(chan struct{})
			t.Log("waiting for dRPC to be ready")
			go func(ctxIn context.Context) {
				for {
					ready := true
					for _, ei := range instances {
						if ei.waitDrpc.IsFalse() {
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
			for _, ei := range instances {
				if tc.dontNotifyReady {
					continue
				}
				req := getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0)
				go func(ctxIn context.Context, i *EngineInstance) {
					select {
					case i.drpcReady <- req:
					case <-ctxIn.Done():
					}
				}(ctx, ei)
				t.Logf("sent drpc ready to instance %d", ei.Index())
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
			for _, ei := range instances {
				dc, err := ei.getDrpcClient()
				if err != nil {
					t.Fatal(err)
				}
				gotDrpcCalls := dc.(*mockDrpcClient).CalledMethods()
				AssertEqual(t, tc.expDrpcCalls[ei.Index()], gotDrpcCalls,
					fmt.Sprintf("%s: unexpected dRPCs for instance %d", name, ei.Index()))

				if diff := cmp.Diff(tc.expGrpcCalls[ei.Index()], joinRequests[ei.Index()]); diff != "" {
					t.Fatalf("unexpected gRPCs for instance %d (-want, +got):\n%s\n",
						ei.Index(), diff)
				}
				rank, _ := ei.GetRank()
				if diff := cmp.Diff(tc.expRanks[ei.Index()], rank); diff != "" {
					t.Fatalf("unexpected rank for instance %d (-want, +got):\n%s\n",
						ei.Index(), diff)
				}
				CmpErr(t, tc.expIoErrs[ei.Index()], ei._lastErr)
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
}*/

func TestServer_Harness_WithFaultDomain(t *testing.T) {
	harness := &EngineHarness{}
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
