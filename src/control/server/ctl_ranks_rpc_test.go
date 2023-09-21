//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"os"
	"sync"
	"syscall"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	// test aliases for member states
	msReady      = stateString(system.MemberStateReady)
	msWaitFormat = stateString(system.MemberStateAwaitFormat)
	msStopped    = stateString(system.MemberStateStopped)
	msErrored    = stateString(system.MemberStateErrored)

	defRankCmpOpts = append(test.DefaultCmpOpts(),
		protocmp.IgnoreFields(&sharedpb.RankResult{}, "msg"),
	)
)

func mockEvtEngineDied(t *testing.T) *events.RASEvent {
	t.Helper()
	return events.NewEngineDiedEvent("foo", 0, 0, common.NormalExit, 1234)
}

// checkUnorderedRankResults fails if results slices contain any differing results,
// regardless of order. Ignore result "Msg" field as RankResult.Msg generation
// is tested separately in TestServer_CtlSvc_DrespToRankResult unit tests.
func checkUnorderedRankResults(t *testing.T, expResults, gotResults []*sharedpb.RankResult) {
	t.Helper()

	test.AssertEqual(t, len(gotResults), len(expResults), "number of rank results")
	for _, exp := range expResults {
		rank := exp.Rank
		match := false
		for _, got := range gotResults {
			if got.Rank == exp.Rank {
				if diff := cmp.Diff(exp, got, defRankCmpOpts...); diff != "" {
					t.Fatalf("unexpected result: %s", diff)
				}
				match = true
			}
		}
		if !match {
			t.Fatalf("unmatched result for rank %d", rank)
		}
	}
}

func TestServer_CtlSvc_PrepShutdownRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		missingSB        bool
		instancesStopped bool
		req              *ctlpb.RanksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		responseDelay    time.Duration
		ctxTimeout       time.Duration
		ctxCancel        time.Duration
		expResults       []*sharedpb.RankResult
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &ctlpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &ctlpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank cannot be read from superblock
			expResults: []*sharedpb.RankResult{},
		},
		"instances stopped already": {
			req:              &ctlpb.RanksReq{Ranks: "0-3"},
			instancesStopped: true,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msStopped},
				{Rank: 2, State: msStopped},
			},
		},
		"dRPC resp fails": {
			req:     &ctlpb.RanksReq{Ranks: "0-3"},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"dRPC resp junk": {
			req:      &ctlpb.RanksReq{Ranks: "0-3"},
			junkResp: true,
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"context timeout": { // dRPC req-resp duration > parent context timeout
			req:           &ctlpb.RanksReq{Ranks: "0-3"},
			responseDelay: 40 * time.Millisecond,
			ctxTimeout:    10 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expErr: context.DeadlineExceeded,
		},
		"context cancel": { // dRPC req-resp duration > when parent context is canceled
			req:           &ctlpb.RanksReq{Ranks: "0-3"},
			responseDelay: 40 * time.Millisecond,
			ctxCancel:     10 * time.Millisecond,
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expErr: context.Canceled,
		},
		"unsuccessful call": {
			req: &ctlpb.RanksReq{Ranks: "0-3"},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: -1},
				&mgmtpb.DaosResp{Status: -1},
			},
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msErrored, Errored: true},
				{Rank: 2, State: msErrored, Errored: true},
			},
		},
		"successful call": {
			req: &ctlpb.RanksReq{Ranks: "0-3"},
			drpcResps: []proto.Message{
				&mgmtpb.DaosResp{Status: 0},
				&mgmtpb.DaosResp{Status: 0},
			},
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: stateString(system.MemberStateStopping)},
				{Rank: 2, State: stateString(system.MemberStateStopping)},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().WithEngines(
				engine.MockConfig().WithTargetCount(1),
				engine.MockConfig().WithTargetCount(1),
			)
			svc := mockControlService(t, log, cfg, nil, nil, nil)
			for i, e := range svc.harness.instances {
				srv := e.(*EngineInstance)
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &engine.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = engine.NewTestRunner(trc, engine.MockConfig())
				srv.setIndex(uint32(i))

				srv._superblock.Rank = new(ranklist.Rank)
				*srv._superblock.Rank = ranklist.Rank(i + 1)

				cfg := new(mockDrpcClientConfig)
				if tc.drpcRet != nil {
					cfg.setSendMsgResponse(drpc.Status_FAILURE, nil, nil)
				} else if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)

					if tc.responseDelay != time.Duration(0) {
						cfg.setResponseDelay(tc.responseDelay)
					}
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			var cancel context.CancelFunc
			ctx := test.Context(t)
			if tc.ctxTimeout != 0 {
				ctx, cancel = context.WithTimeout(ctx, tc.ctxTimeout)
				defer cancel()
			} else if tc.ctxCancel != 0 {
				ctx, cancel = context.WithCancel(ctx)
				go func() {
					<-time.After(tc.ctxCancel)
					cancel()
				}()
			}

			gotResp, gotErr := svc.PrepShutdownRanks(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// order of results nondeterministic as dPrepShutdown run async
			checkUnorderedRankResults(t, tc.expResults, gotResp.Results)
		})
	}
}

func TestServer_CtlSvc_StopRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		missingSB         bool
		engineCount       int
		instancesStopped  bool
		instancesDontStop bool
		req               *ctlpb.RanksReq
		timeout           time.Duration
		signal            os.Signal
		expSignalsSent    map[uint32]os.Signal
		expResults        []*sharedpb.RankResult
		expErr            error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &ctlpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &ctlpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank cannot be read from superblock
			expResults: []*sharedpb.RankResult{},
		},
		"missing ranks": {
			req:        &ctlpb.RanksReq{Ranks: "0,3"},
			expResults: []*sharedpb.RankResult{},
		},
		"instances successfully stopped": {
			req:            &ctlpb.RanksReq{Ranks: "0-3"},
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGINT, 1: syscall.SIGINT},
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msStopped},
				{Rank: 2, State: msStopped},
			},
		},
		"instances successfully stopped with force": {
			req:            &ctlpb.RanksReq{Ranks: "0-3", Force: true},
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGKILL, 1: syscall.SIGKILL},
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msStopped},
				{Rank: 2, State: msStopped},
			},
		},
		"instances not stopped in time": {
			req:               &ctlpb.RanksReq{Ranks: "0-3"},
			timeout:           time.Second,
			expSignalsSent:    map[uint32]os.Signal{0: syscall.SIGINT, 1: syscall.SIGINT},
			instancesDontStop: true,
			expErr:            errors.New("deadline exceeded"),
		},
		"instances already stopped": { // successful result for kill
			req:              &ctlpb.RanksReq{Ranks: "0-3"},
			instancesStopped: true,
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msStopped},
				{Rank: 2, State: msStopped},
			},
		},
		"single instance already stopped": {
			req:              &ctlpb.RanksReq{Ranks: "1"},
			instancesStopped: true,
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msStopped},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var signalsSent sync.Map

			if tc.engineCount == 0 {
				tc.engineCount = maxEngines
			}

			cfg := config.DefaultServer().WithEngines(
				engine.MockConfig().WithTargetCount(1),
				engine.MockConfig().WithTargetCount(1),
			)
			svc := mockControlService(t, log, cfg, nil, nil, nil)

			ctx, outerCancel := context.WithCancel(test.Context(t))
			t.Cleanup(outerCancel)
			var cancel context.CancelFunc
			if tc.timeout != time.Duration(0) {
				t.Logf("timeout of %s being applied", tc.timeout)
				ctx, cancel = context.WithTimeout(ctx, tc.timeout)
				defer cancel()
			}

			ps := events.NewPubSub(ctx, log)
			defer ps.Close()
			svc.events = ps

			dispatched := &eventsDispatched{cancel: cancel}
			svc.events.Subscribe(events.RASTypeStateChange, dispatched)

			for i, e := range svc.harness.instances {
				ei := e.(*EngineInstance)
				if tc.missingSB {
					ei._superblock = nil
					continue
				}

				trc := &engine.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
					ei.ready.SetTrue()
				}
				trc.SignalCb = func(idx uint32, sig os.Signal) {
					signalsSent.Store(idx, sig)
					if tc.instancesDontStop {
						return
					}
					// simulate process exit which will call onInstanceExit handlers
					ei.handleExit(ctx, 0, common.NormalExit)
					// set false on test runner so IsStarted on engine returns false
					ei.runner.(*engine.TestRunner).GetRunnerConfig().Running.SetFalse()
					log.Debugf("mock handling signal %v on engine %d", sig, idx)
				}
				ei.runner = engine.NewTestRunner(trc, engine.MockConfig())
				ei.setIndex(uint32(i))

				ei._superblock.Rank = new(ranklist.Rank)
				*ei._superblock.Rank = ranklist.Rank(i + 1)

				ei.OnInstanceExit(
					func(_ context.Context, _ uint32, _ ranklist.Rank, _ error, _ int) error {
						svc.events.Publish(mockEvtEngineDied(t))
						return nil
					})
			}

			gotResp, gotErr := svc.StopRanks(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if tc.timeout != time.Duration(0) {
				<-ctx.Done()
			}
			test.AssertEqual(t, 0, len(dispatched.rx), "number of events published")

			if diff := cmp.Diff(tc.expResults, gotResp.Results, defRankCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}

			var numSignalsSent int
			signalsSent.Range(func(_, _ interface{}) bool {
				numSignalsSent++
				return true
			})
			test.AssertEqual(t, len(tc.expSignalsSent), numSignalsSent, "number of signals sent")

			for expKey, expValue := range tc.expSignalsSent {
				value, found := signalsSent.Load(expKey)
				if !found {
					t.Fatalf("rank %d was not sent %s signal", expKey, expValue)
				}
				if diff := cmp.Diff(expValue, value); diff != "" {
					t.Fatalf("unexpected signals sent (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestServer_CtlSvc_ResetFormatRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		missingSB        bool
		engineCount      int
		instancesStarted bool
		startFails       bool
		req              *ctlpb.RanksReq
		timeout          time.Duration
		expResults       []*sharedpb.RankResult
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &ctlpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &ctlpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank can't be read from superblock
			expResults: []*sharedpb.RankResult{},
		},
		"instances already started": {
			req:              &ctlpb.RanksReq{Ranks: "0-3"},
			instancesStarted: true,
			expErr:           FaultInstancesNotStopped("reset format", 1),
		},
		"instances reach wait format": {
			req: &ctlpb.RanksReq{Ranks: "0-3"},
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msWaitFormat},
				{Rank: 2, State: msWaitFormat},
			},
		},
		"instances stay stopped": {
			req:        &ctlpb.RanksReq{Ranks: "0-3"},
			startFails: true,
			timeout:    time.Second,
			expErr:     errors.New("deadline exceeded"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.engineCount == 0 {
				tc.engineCount = maxEngines
			}

			ctx, outerCancel := context.WithCancel(test.Context(t))
			t.Cleanup(outerCancel)
			if tc.timeout != time.Duration(0) {
				t.Logf("timeout of %s being applied", tc.timeout)
				inner, cancel := context.WithTimeout(ctx, tc.timeout)
				defer cancel()
				ctx = inner
			}

			cfg := config.DefaultServer().WithEngines(
				engine.MockConfig().
					WithTargetCount(1).
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram"),
					),
				engine.MockConfig().
					WithTargetCount(1).
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram"),
					),
			)
			svc := mockControlService(t, log, cfg, nil, nil, nil)

			for i, e := range svc.harness.instances {
				ei := e.(*EngineInstance)
				if tc.missingSB {
					ei._superblock = nil
					continue
				}

				engineCfg := cfg.Engines[i]

				testDir, cleanup := test.CreateTestDir(t)
				defer cleanup()
				engineCfg.Storage.Tiers[0].Scm.MountPoint = testDir

				trc := &engine.TestRunnerConfig{}
				if tc.instancesStarted {
					trc.Running.SetTrue()
					ei.ready.SetTrue()
				}
				if !tc.startFails {
					ei.waitFormat.SetTrue()
				}

				ei.runner = engine.NewTestRunner(trc, engineCfg)
				ei.setIndex(uint32(i))

				cfg, err := ei.storage.GetScmConfig()
				if err != nil {
					t.Fatal(err)
				}
				t.Logf("scm dir: %s", cfg.Scm.MountPoint)
				superblock := &Superblock{
					Version: superblockVersion,
					UUID:    test.MockUUID(),
					System:  "test",
				}
				superblock.Rank = new(ranklist.Rank)
				*superblock.Rank = ranklist.Rank(i + 1)
				ei.setSuperblock(superblock)
				if err := ei.WriteSuperblock(); err != nil {
					t.Fatal(err)
				}

				// Unblock requestStart() called from ResetFormatRanks() by reading
				// from startRequested channel.
				go func(s *EngineInstance, startFails bool) {
					select {
					case <-ctx.Done():
					case <-s.startRequested:
					}
				}(ei, tc.startFails)
			}

			gotResp, gotErr := svc.ResetFormatRanks(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResults, gotResp.Results, defRankCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_CtlSvc_StartRanks(t *testing.T) {
	for name, tc := range map[string]struct {
		missingSB        bool
		engineCount      int
		instancesStopped bool
		startFails       bool
		req              *ctlpb.RanksReq
		timeout          time.Duration
		expResults       []*sharedpb.RankResult
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"no ranks specified": {
			req:    &ctlpb.RanksReq{},
			expErr: errors.New("no ranks specified in request"),
		},
		"missing superblock": {
			req:       &ctlpb.RanksReq{Ranks: "0-3"},
			missingSB: true,
			// no results as rank cannot be read from superblock
			expResults: []*sharedpb.RankResult{},
		},
		"missing ranks": {
			req:        &ctlpb.RanksReq{Ranks: "0,3"},
			expResults: []*sharedpb.RankResult{},
		},
		"instances already started": {
			req: &ctlpb.RanksReq{Ranks: "0-3"},
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msReady},
				{Rank: 2, State: msReady},
			},
		},
		"instances get started": {
			req:              &ctlpb.RanksReq{Ranks: "0-3"},
			instancesStopped: true,
			expResults: []*sharedpb.RankResult{
				{Rank: 1, State: msReady},
				{Rank: 2, State: msReady},
			},
		},
		"instances not started in time": {
			req:              &ctlpb.RanksReq{Ranks: "0-3"},
			timeout:          time.Second,
			instancesStopped: true,
			startFails:       true,
			expErr:           errors.New("deadline exceeded"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.engineCount == 0 {
				tc.engineCount = maxEngines
			}

			ctx, outerCancel := context.WithCancel(test.Context(t))
			t.Cleanup(outerCancel)
			if tc.timeout != time.Duration(0) {
				t.Logf("timeout of %s being applied", tc.timeout)
				inner, cancel := context.WithTimeout(ctx, tc.timeout)
				defer cancel()
				ctx = inner
			}

			cfg := config.DefaultServer().WithEngines(
				engine.MockConfig().WithTargetCount(1),
				engine.MockConfig().WithTargetCount(1),
			)
			svc := mockControlService(t, log, cfg, nil, nil, nil)

			for i, e := range svc.harness.instances {
				srv := e.(*EngineInstance)
				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				trc := &engine.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = engine.NewTestRunner(trc, engine.MockConfig())
				srv.setIndex(uint32(i))

				srv._superblock.Rank = new(ranklist.Rank)
				*srv._superblock.Rank = ranklist.Rank(i + 1)

				// mimic srv.run, set "ready" on startLoop rx
				go func(ctx context.Context, s *EngineInstance, startFails bool) {
					select {
					case <-ctx.Done():
						return
					case <-s.startRequested:
					}
					t.Logf("instance %d: start signal received", s.Index())
					if startFails || !tc.instancesStopped {
						return
					}

					// set instance runner started and ready
					if _, err := s.runner.Start(ctx); err != nil {
						t.Logf("failed to start runner: %s", err)
						return
					}
					s.ready.SetTrue()
					t.Log("ready set to true")
				}(ctx, srv, tc.startFails)
			}

			gotResp, gotErr := svc.StartRanks(ctx, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResults, gotResp.Results, defRankCmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_updateSetEngineLogMasksReq(t *testing.T) {
	for name, tc := range map[string]struct {
		req           ctlpb.SetLogMasksReq
		cfgMasks      string
		cfgStreams    string
		cfgSubsystems string
		expMasks      string
		expStreams    string
		expSubsystems string
		expErr        error
	}{
		"empty masks string in request; reset not set in request": {
			expErr: errors.New("empty log masks in request"),
		},
		"empty masks string in request; no configured log mask": {
			req: ctlpb.SetLogMasksReq{
				ResetMasks: true,
			},
			expErr: errors.New("empty log masks in config"),
		},
		"empty masks string in request; configured log mask": {
			req: ctlpb.SetLogMasksReq{
				Masks:      "DEBUG",
				ResetMasks: true,
			},
			cfgMasks: "ERR",
			expMasks: "ERR",
		},
		"masks specified in request": {
			req: ctlpb.SetLogMasksReq{
				Masks: "DEBUG",
			},
			cfgMasks: "ERR",
			expMasks: "DBUG",
		},
		"all values specified in request": {
			req: ctlpb.SetLogMasksReq{
				Masks:      "DEBUG",
				Streams:    "MGMT",
				Subsystems: "MISC",
			},
			expMasks:   "ERR,MISC=DBUG",
			expStreams: "MGMT",
		},
		"reset all masks; simple": {
			req: ctlpb.SetLogMasksReq{
				Masks:           "DEBUG",
				Streams:         "MGMT",
				Subsystems:      "MISC",
				ResetMasks:      true,
				ResetStreams:    true,
				ResetSubsystems: true,
			},
			cfgMasks:      "ERR",
			cfgStreams:    "md",
			cfgSubsystems: "misc",
			expMasks:      "ERR",
			expStreams:    "md",
		},
		"reset all masks; complex": {
			req: ctlpb.SetLogMasksReq{
				ResetMasks:      true,
				ResetStreams:    true,
				ResetSubsystems: true,
			},
			cfgMasks:   "info,dtx=debug,vos=debug,object=debug",
			cfgStreams: "io,epc",
			expMasks:   "INFO,dtx=DBUG,vos=DBUG,object=DBUG",
			expStreams: "io,epc",
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := engine.MockConfig().
				WithLogMask(tc.cfgMasks).
				WithLogStreams(tc.cfgStreams).
				WithLogSubsystems(tc.cfgSubsystems)

			gotErr := updateSetLogMasksReq(cfg, &tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expMasks, tc.req.Masks); diff != "" {
				t.Fatalf("unexpected masks: %s", diff)
			}
			if diff := cmp.Diff(tc.expStreams, tc.req.Streams); diff != "" {
				t.Fatalf("unexpected streams: %s", diff)
			}
			if diff := cmp.Diff(tc.expSubsystems, tc.req.Subsystems); diff != "" {
				t.Fatalf("unexpected subsystems: %s", diff)
			}
		})
	}
}

func TestServer_CtlSvc_SetEngineLogMasks(t *testing.T) {
	for name, tc := range map[string]struct {
		missingRank      bool
		instancesStopped bool
		cfgLogMask       string
		req              *ctlpb.SetLogMasksReq
		drpcRet          error
		junkResp         bool
		drpcResps        []proto.Message
		responseDelay    time.Duration
		expResp          *ctlpb.SetLogMasksResp
		expErr           error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"empty masks string in request; configured log mask": {
			cfgLogMask: "DEBUG",
			req: &ctlpb.SetLogMasksReq{
				ResetMasks: true,
			},
			expResp: &ctlpb.SetLogMasksResp{
				Errors: []string{
					"validate response: dRPC returned no response",
					"validate response: dRPC returned no response",
				},
			},
		},
		"instances stopped": {
			req:              &ctlpb.SetLogMasksReq{Masks: "ERR,mgmt=DEBUG"},
			instancesStopped: true,
			expResp: &ctlpb.SetLogMasksResp{
				Errors: []string{
					FaultDataPlaneNotStarted.Error(),
					FaultDataPlaneNotStarted.Error(),
				},
			},
		},
		"dRPC resp fails": {
			req:     &ctlpb.SetLogMasksReq{Masks: "ERR,mgmt=DEBUG"},
			drpcRet: errors.New("call failed"),
			drpcResps: []proto.Message{
				&ctlpb.SetLogMasksResp{Status: 0},
				&ctlpb.SetLogMasksResp{Status: 0},
			},
			expResp: &ctlpb.SetLogMasksResp{
				Errors: []string{
					"validate response: bad dRPC response status: FAILURE",
					"validate response: bad dRPC response status: FAILURE",
				},
			},
		},
		"dRPC resp junk": {
			req:      &ctlpb.SetLogMasksReq{Masks: "ERR,mgmt=DEBUG"},
			junkResp: true,
			expErr:   errors.New("invalid wire-format data"),
		},
		"missing superblock": { // shouldn't matter in this case
			req:         &ctlpb.SetLogMasksReq{Masks: "ERR,mgmt=DEBUG"},
			missingRank: true,
			drpcResps: []proto.Message{
				&ctlpb.SetLogMasksResp{Status: 0},
				&ctlpb.SetLogMasksResp{Status: 0},
			},
			expResp: &ctlpb.SetLogMasksResp{
				Errors: []string{"", ""},
			},
		},
		"successful call": {
			req: &ctlpb.SetLogMasksReq{Masks: "ERR,mgmt=DEBUG"},
			drpcResps: []proto.Message{
				&ctlpb.SetLogMasksResp{Status: 0},
				&ctlpb.SetLogMasksResp{Status: 0},
			},
			expResp: &ctlpb.SetLogMasksResp{
				Errors: []string{"", ""},
			},
		},
		"unsuccessful call": {
			req: &ctlpb.SetLogMasksReq{Masks: "ERR,mgmt=DEBUG"},
			drpcResps: []proto.Message{
				&ctlpb.SetLogMasksResp{Status: -1},
				&ctlpb.SetLogMasksResp{Status: -1},
			},
			expResp: &ctlpb.SetLogMasksResp{
				Errors: []string{
					"DER_UNKNOWN(-1): Unknown error code -1",
					"DER_UNKNOWN(-1): Unknown error code -1",
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer().WithEngines(
				engine.MockConfig().WithTargetCount(1).WithLogMask(tc.cfgLogMask),
				engine.MockConfig().WithTargetCount(1).WithLogMask(tc.cfgLogMask),
			)
			svc := mockControlService(t, log, cfg, nil, nil, nil)
			for i, e := range svc.harness.instances {
				srv := e.(*EngineInstance)

				trc := &engine.TestRunnerConfig{}
				if !tc.instancesStopped {
					trc.Running.SetTrue()
					srv.ready.SetTrue()
				}
				srv.runner = engine.NewTestRunner(trc, engine.MockConfig())
				srv.setIndex(uint32(i))

				if !tc.missingRank {
					srv._superblock.Rank = new(ranklist.Rank)
					*srv._superblock.Rank = ranklist.Rank(i + 1)
				}

				cfg := new(mockDrpcClientConfig)
				if tc.drpcRet != nil {
					cfg.setSendMsgResponse(drpc.Status_FAILURE, nil, nil)
				} else if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					rb, _ := proto.Marshal(tc.drpcResps[i])
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, rb, tc.expErr)

					if tc.responseDelay != time.Duration(0) {
						cfg.setResponseDelay(tc.responseDelay)
					}
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}

			gotResp, gotErr := svc.SetEngineLogMasks(test.Context(t), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defRankCmpOpts...); diff != "" {
				t.Fatalf("unexpected results: %s", diff)
			}
		})
	}
}
