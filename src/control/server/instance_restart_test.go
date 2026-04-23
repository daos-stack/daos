//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

func TestServer_EngineRestartManager_GetMinDelay(t *testing.T) {
	for name, tc := range map[string]struct {
		configDelay int
		expDelay    time.Duration
	}{
		"default delay": {
			configDelay: 0,
			expDelay:    300 * time.Second,
		},
		"custom delay": {
			configDelay: 60,
			expDelay:    60 * time.Second,
		},
		"long delay": {
			configDelay: 600,
			expDelay:    600 * time.Second,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cfg := &config.Server{
				EngineAutoRestartMinDelay: tc.configDelay,
			}

			mgr := newEngineRestartManager(log, cfg)

			gotDelay := mgr.getMinDelay()
			if gotDelay != tc.expDelay {
				t.Errorf("expected delay %s, got %s", tc.expDelay, gotDelay)
			}
		})
	}
}

func TestServer_EngineRestartManager_CanRestartNow(t *testing.T) {
	for name, tc := range map[string]struct {
		lastRestartAge time.Duration
		minDelay       int
		expCanRestart  bool
	}{
		"no previous restart": {
			lastRestartAge: 0,
			minDelay:       60,
			expCanRestart:  true,
		},
		"enough time elapsed": {
			lastRestartAge: 70 * time.Second,
			minDelay:       60,
			expCanRestart:  true,
		},
		"not enough time elapsed": {
			lastRestartAge: 50 * time.Second,
			minDelay:       60,
			expCanRestart:  false,
		},
		"exactly minimum delay": {
			lastRestartAge: 60 * time.Second,
			minDelay:       60,
			expCanRestart:  true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cfg := &config.Server{
				EngineAutoRestartMinDelay: tc.minDelay,
			}

			mgr := newEngineRestartManager(log, cfg)
			testRank := ranklist.Rank(1)

			// Set last restart time if test case specifies
			if tc.lastRestartAge > 0 {
				mgr.lastRestart[testRank] = time.Now().Add(-tc.lastRestartAge)
			}

			canRestart, remaining := mgr.canRestartNow(testRank)

			if canRestart != tc.expCanRestart {
				t.Errorf("expected canRestart=%v, got %v", tc.expCanRestart,
					canRestart)
			}

			if tc.expCanRestart && remaining != 0 {
				t.Errorf("expected no remaining delay when can restart, got %s",
					remaining)
			}

			if !tc.expCanRestart && remaining <= 0 {
				t.Errorf("expected positive remaining delay when cannot restart, "+
					"got %s", remaining)
			}
		})
	}
}

func TestServer_EngineRestartManager_RecordRestartTime(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := &config.Server{}
	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)

	beforeRecord := time.Now()
	mgr.recordRestartTime(testRank)
	afterRecord := time.Now()

	recordedTime, exists := mgr.lastRestart[testRank]
	if !exists {
		t.Fatal("restart time not recorded")
	}

	if recordedTime.Before(beforeRecord) || recordedTime.After(afterRecord) {
		t.Errorf("recorded time %s outside expected range [%s, %s]",
			recordedTime, beforeRecord, afterRecord)
	}
}

func TestServer_EngineRestartManager_SetPendingRestart(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := &config.Server{}
	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)

	// Set initial timer
	timer1 := time.NewTimer(10 * time.Second)
	mgr.setPendingRestart(testRank, timer1)

	if len(mgr.pendingRestart) != 1 {
		t.Fatalf("expected 1 pending restart, got %d", len(mgr.pendingRestart))
	}

	// Set another timer for same rank (should cancel previous)
	timer2 := time.NewTimer(5 * time.Second)
	mgr.setPendingRestart(testRank, timer2)

	if len(mgr.pendingRestart) != 1 {
		t.Fatalf("expected 1 pending restart after replacement, got %d",
			len(mgr.pendingRestart))
	}

	if mgr.pendingRestart[testRank] != timer2 {
		t.Error("pending restart timer not updated to new timer")
	}

	// Cleanup
	timer2.Stop()
}

func TestServer_EngineRestartManager_ClearPendingRestart(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := &config.Server{}
	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)

	// Set a timer
	timer := time.NewTimer(10 * time.Second)
	defer timer.Stop()
	mgr.pendingRestart[testRank] = timer

	// Clear it
	mgr.clearPendingRestart(testRank)

	if len(mgr.pendingRestart) != 0 {
		t.Errorf("expected no pending restarts after clear, got %d",
			len(mgr.pendingRestart))
	}
}

func TestServer_EngineRestartManager_RequestRestart(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := &config.Server{}
	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)

	// Create mock instance
	mockInstance := &MockInstance{
		cfg: MockInstanceConfig{
			GetRankResp: testRank,
		},
	}

	mgr.requestRestart(testRank, mockInstance)

	// Should receive request on channel
	select {
	case req := <-mgr.requestChan:
		if !req.rank.Equals(testRank) {
			t.Errorf("expected rank %d, got %d", testRank, req.rank)
		}
		if req.instance != mockInstance {
			t.Error("expected mock instance in request")
		}
	case <-time.After(1 * time.Second):
		t.Fatal("timeout waiting for restart request")
	}
}

func TestServer_EngineRestartManager_RequestRestart_ChannelFull(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := &config.Server{}
	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)

	mockInstance := &MockInstance{
		cfg: MockInstanceConfig{
			GetRankResp: testRank,
		},
	}

	// Fill the channel
	for i := 0; i < engineRestartMaxQueueSz; i++ {
		mgr.requestRestart(ranklist.Rank(i), mockInstance)
	}

	// Next request should be dropped
	mgr.requestRestart(testRank, mockInstance)

	// Should see error in log
	logOutput := buf.String()
	if !strings.Contains(logOutput, "channel full") &&
		!strings.Contains(logOutput, "dropping request") {
		t.Error("expected channel full error in log")
	}
}

func TestServer_EngineRestartManager_ProcessRestartRequest_Immediate(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx := test.Context(t)
	cfg := &config.Server{
		EngineAutoRestartMinDelay: 10,
	}

	harness := NewEngineHarness(log)
	setupTestEngine(t, log, harness, false)

	instances, err := harness.FilterInstancesByRankSet("1")
	if err != nil || len(instances) == 0 {
		t.Fatalf("failed to get instance: %v", err)
	}

	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)
	instance := instances[0]

	// Run go-routine for engine to consume from startRequested channel otherwise
	// requestStart() instance methods would block
	go func(inCtx context.Context, e *EngineInstance) {
		select {
		case <-inCtx.Done():
		case <-e.startRequested:
		}
	}(ctx, instance.(*EngineInstance))

	req := engineRestartRequest{
		rank:     testRank,
		instance: instance,
	}

	// Process request (no previous restart, should be immediate)
	mgr.processRestartRequest(ctx, req)

	// Verify restart time recorded
	mgr.mu.Lock()
	_, recorded := mgr.lastRestart[testRank]
	mgr.mu.Unlock()

	if !recorded {
		t.Error("expected restart time to be recorded")
	}

	// Verify no pending restart
	mgr.mu.Lock()
	_, pending := mgr.pendingRestart[testRank]
	mgr.mu.Unlock()

	if pending {
		t.Error("expected no pending restart for immediate restart")
	}
}

func TestServer_EngineRestartManager_ProcessRestartRequest_Deferred(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx := test.Context(t)
	cfg := &config.Server{
		EngineAutoRestartMinDelay: 2, // 2 seconds for fast test
	}

	harness := NewEngineHarness(log)
	setupTestEngine(t, log, harness, false)

	instances, err := harness.FilterInstancesByRankSet("1")
	if err != nil || len(instances) == 0 {
		t.Fatalf("failed to get instance: %v", err)
	}

	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)

	// Record a recent restart
	mgr.lastRestart[testRank] = time.Now()

	req := engineRestartRequest{
		rank:     testRank,
		instance: instances[0],
	}

	// Process request (should be deferred due to rate limiting)
	mgr.processRestartRequest(ctx, req)

	// Verify pending restart was set
	mgr.mu.RLock()
	timer, pending := mgr.pendingRestart[testRank]
	mgr.mu.RUnlock()

	if !pending {
		t.Fatal("expected pending restart to be set")
	}

	// Cleanup
	if timer != nil {
		timer.Stop()
	}
	mgr.clearPendingRestart(testRank)

	// Verify log message
	logOutput := buf.String()
	if !strings.Contains(logOutput, "rate limited") &&
		!strings.Contains(logOutput, "will restart in") {
		t.Error("expected rate limited message in log")
	}
}

func TestServer_EngineRestartManager_Stop(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := &config.Server{}
	mgr := newEngineRestartManager(log, cfg)

	// Add some pending restarts
	timer1 := time.NewTimer(10 * time.Second)
	timer2 := time.NewTimer(10 * time.Second)
	mgr.pendingRestart[ranklist.Rank(1)] = timer1
	mgr.pendingRestart[ranklist.Rank(2)] = timer2

	// Stop should cancel all timers
	mgr.stop()

	if len(mgr.pendingRestart) != 0 {
		t.Errorf("expected all pending restarts cleared, got %d",
			len(mgr.pendingRestart))
	}

	// Verify stopChan is closed
	select {
	case <-mgr.stopChan:
		// Expected
	default:
		t.Error("stopChan should be closed")
	}
}

func TestServer_EngineRestartManager_Start_ProcessRequests(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithTimeout(test.Context(t), 10*time.Second)
	defer cancel()

	cfg := &config.Server{
		EngineAutoRestartMinDelay: 10,
	}

	harness := NewEngineHarness(log)
	setupTestEngine(t, log, harness, false)

	instances, err := harness.FilterInstancesByRankSet("1")
	if err != nil || len(instances) == 0 {
		t.Fatalf("failed to get instance: %v", err)
	}

	mgr := newEngineRestartManager(log, cfg)
	mgr.start(ctx)
	defer mgr.stop()

	testRank := ranklist.Rank(1)
	instance := instances[0]

	// Run go-routine for engine to consume from startRequested channel otherwise
	// requestStart() instance methods would block
	go func(inCtx context.Context, e *EngineInstance) {
		select {
		case <-inCtx.Done():
		case <-e.startRequested:
		}
	}(ctx, instance.(*EngineInstance))

	// Channel to signal when restart is recorded
	recorded := make(chan struct{})
	go func(inCtx context.Context) {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()

		for {
			select {
			case <-inCtx.Done():
				return
			case <-ticker.C:
				mgr.mu.RLock()
				_, exists := mgr.lastRestart[testRank]
				mgr.mu.RUnlock()

				if exists {
					close(recorded)
					return
				}
			}
		}
	}(ctx)

	// Submit restart request
	mgr.requestRestart(testRank, instance)

	// Wait for restart time to be recorded
	select {
	case <-ctx.Done():
		t.Error("expected restart time to be recorded after processing")
	case <-recorded:
		t.Log("restart time recorded successfully")
	}
}

func TestServer_EngineRestartManager_DeferredRestartExecutes(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, cancel := context.WithTimeout(test.Context(t), 20*time.Second)
	defer cancel()

	cfg := &config.Server{
		EngineAutoRestartMinDelay: 2, // seconds
	}

	harness := NewEngineHarness(log)
	setupTestEngine(t, log, harness, false)

	instances, err := harness.FilterInstancesByRankSet("1")
	if err != nil || len(instances) == 0 {
		t.Fatalf("failed to get instance: %v", err)
	}

	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)
	instance := instances[0]

	// Run go-routine for engine to consume from startRequested channel otherwise
	// requestStart() instance methods would block
	go func(inCtx context.Context, e *EngineInstance) {
		select {
		case <-inCtx.Done():
		case <-e.startRequested:
		}
	}(ctx, instance.(*EngineInstance))

	// Set recent restart time
	mgr.lastRestart[testRank] = time.Now()

	req := engineRestartRequest{
		rank:     testRank,
		instance: instance,
	}

	// Process request (should create deferred restart)
	mgr.processRestartRequest(ctx, req)

	// Verify timer exists
	mgr.mu.RLock()
	timer, exists := mgr.pendingRestart[testRank]
	mgr.mu.RUnlock()

	if !exists {
		t.Fatal("expected pending restart timer to be created")
	}

	// Wait for timer to fire (with buffer)
	time.Sleep(10 * time.Second)

	// Verify timer was cleaned up
	mgr.mu.RLock()
	_, stillPending := mgr.pendingRestart[testRank]
	mgr.mu.RUnlock()

	if stillPending {
		t.Error("expected pending restart to be cleared after execution")
	}

	// Cleanup
	if timer != nil {
		timer.Stop()
	}
}

func TestServer_EngineRestartManager_MultipleRanks(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := &config.Server{
		EngineAutoRestartMinDelay: 10,
	}

	mgr := newEngineRestartManager(log, cfg)

	rank1 := ranklist.Rank(1)
	rank2 := ranklist.Rank(2)

	// Record restarts for both ranks
	mgr.recordRestartTime(rank1)
	time.Sleep(10 * time.Millisecond)
	mgr.recordRestartTime(rank2)

	// Verify both recorded
	mgr.mu.RLock()
	time1, exists1 := mgr.lastRestart[rank1]
	time2, exists2 := mgr.lastRestart[rank2]
	mgr.mu.RUnlock()

	if !exists1 || !exists2 {
		t.Fatal("expected both ranks to have restart times recorded")
	}

	if !time1.Before(time2) {
		t.Error("expected rank1 restart time to be before rank2")
	}

	// Verify independent rate limiting
	canRestart1, _ := mgr.canRestartNow(rank1)
	canRestart2, _ := mgr.canRestartNow(rank2)

	if canRestart1 || canRestart2 {
		t.Error("expected both ranks to be rate limited")
	}
}

func TestServer_EngineRestartManager_CancelExistingTimer(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx := test.Context(t)
	cfg := &config.Server{
		EngineAutoRestartMinDelay: 5,
	}

	harness := NewEngineHarness(log)
	setupTestEngine(t, log, harness, false)

	instances, err := harness.FilterInstancesByRankSet("1")
	if err != nil || len(instances) == 0 {
		t.Fatalf("failed to get instance: %v", err)
	}

	mgr := newEngineRestartManager(log, cfg)
	testRank := ranklist.Rank(1)

	// Set recent restart
	mgr.lastRestart[testRank] = time.Now()

	// First deferred request
	req1 := engineRestartRequest{
		rank:     testRank,
		instance: instances[0],
	}
	mgr.processRestartRequest(ctx, req1)

	mgr.mu.RLock()
	timer1, exists1 := mgr.pendingRestart[testRank]
	mgr.mu.RUnlock()

	if !exists1 {
		t.Fatal("expected first pending restart to be set")
	}

	// Second deferred request (should cancel first)
	time.Sleep(100 * time.Millisecond)
	req2 := engineRestartRequest{
		rank:     testRank,
		instance: instances[0],
	}
	mgr.processRestartRequest(ctx, req2)

	mgr.mu.RLock()
	timer2, exists2 := mgr.pendingRestart[testRank]
	mgr.mu.RUnlock()

	if !exists2 {
		t.Fatal("expected second pending restart to be set")
	}

	if timer1 == timer2 {
		t.Error("expected timer to be replaced")
	}

	// Verify log shows cancellation
	logOutput := buf.String()
	if !strings.Contains(logOutput, "cancelled existing pending restart") {
		t.Error("expected cancellation message in log")
	}

	// Cleanup
	if timer2 != nil {
		timer2.Stop()
	}
	mgr.clearPendingRestart(testRank)
}

func TestServer_NewEngineRestartManager(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := &config.Server{
		EngineAutoRestartMinDelay: 42,
	}

	mgr := newEngineRestartManager(log, cfg)

	if mgr.log == nil {
		t.Error("expected logger to be set")
	}

	if mgr.cfg != cfg {
		t.Error("expected config to be set")
	}

	if mgr.requestChan == nil {
		t.Error("expected requestChan to be initialized")
	}

	if cap(mgr.requestChan) != engineRestartMaxQueueSz {
		t.Errorf("expected channel capacity %d, got %d",
			engineRestartMaxQueueSz, cap(mgr.requestChan))
	}

	if mgr.stopChan == nil {
		t.Error("expected stopChan to be initialized")
	}

	if mgr.lastRestart == nil {
		t.Error("expected lastRestart map to be initialized")
	}

	if mgr.pendingRestart == nil {
		t.Error("expected pendingRestart map to be initialized")
	}
}
