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

// Test helper functions

func setupTestLogger(t *testing.T) (logging.Logger, *logging.LogBuffer) {
	t.Helper()
	log, buf := logging.NewTestLogger(t.Name())
	t.Cleanup(func() {
		test.ShowBufferOnFailure(t, buf)
	})

	return log, buf
}

func getTestLogger(t *testing.T, loggers []logging.Logger) logging.Logger {
	t.Helper()
	var log logging.Logger

	switch len(loggers) {
	case 0:
		log, _ = setupTestLogger(t)
	case 1:
		log = loggers[0]
	default:
		t.Fatal("multiple loggers provided, want one")
	}

	return log
}

func setupTestManager(t *testing.T, cfg *config.Server, loggers ...logging.Logger) *engineRestartManager {
	t.Helper()
	log := getTestLogger(t, loggers)
	if cfg == nil {
		cfg = &config.Server{}
	}

	return newEngineRestartManager(log, cfg)
}

func setupTestHarness(t *testing.T, rankStr string, loggers ...logging.Logger) (*EngineInstance, ranklist.Rank) {
	t.Helper()
	log := getTestLogger(t, loggers)
	harness := NewEngineHarness(log)

	// Parse the rank from the string to pass to setupAddTestEngine
	ranks, err := ranklist.ParseRanks(rankStr)
	if err != nil || len(ranks) != 1 {
		t.Fatalf("failed to parse rank: %v", err)
	}
	rankNum := uint32(ranks[0])

	setupAddTestEngine(t, log, harness, false, rankNum)

	instances, err := harness.FilterInstancesByRankSet(rankStr)
	if err != nil || len(instances) == 0 {
		t.Fatalf("failed to get instance: %v", err)
	}

	return instances[0].(*EngineInstance), ranks[0]
}

func startInstanceConsumer(ctx context.Context, instance *EngineInstance) {
	go func() {
		select {
		case <-ctx.Done():
		case <-instance.startRequested:
		}
	}()
}

func waitForPendingRestart(ctx context.Context, t *testing.T, mgr *engineRestartManager, rank ranklist.Rank) bool {
	t.Helper()
	pending := make(chan struct{})
	go func() {
		ticker := time.NewTicker(50 * time.Millisecond)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				mgr.mu.RLock()
				_, exists := mgr.pendingRestart[rank]
				mgr.mu.RUnlock()

				if exists {
					close(pending)
					return
				}
			}
		}
	}()

	select {
	case <-ctx.Done():
		return false
	case <-pending:
		return true
	}
}

func waitForRestartRecorded(ctx context.Context, t *testing.T, mgr *engineRestartManager, rank ranklist.Rank) bool {
	recorded := make(chan struct{})
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				mgr.mu.RLock()
				_, exists := mgr.lastRestart[rank]
				mgr.mu.RUnlock()

				if exists {
					close(recorded)
					return
				}
			}
		}
	}()

	select {
	case <-ctx.Done():
		return false
	case <-recorded:
		return true
	}
}

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
			mgr := setupTestManager(t, &config.Server{
				EngineAutoRestartMinDelay: tc.configDelay,
			})

			gotDelay := mgr.getMinDelay()
			if gotDelay != tc.expDelay {
				t.Errorf("expected delay %s, got %s", tc.expDelay, gotDelay)
			}
		})
	}
}

func TestServer_EngineRestartManager_RequestRestart(t *testing.T) {
	mgr := setupTestManager(t, nil)
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
	log, buf := setupTestLogger(t)
	mgr := setupTestManager(t, nil, log)
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
	ctx := test.Context(t)
	instance, testRank := setupTestHarness(t, "1")
	mgr := setupTestManager(t, &config.Server{
		EngineAutoRestartMinDelay: 10,
	})

	startInstanceConsumer(ctx, instance)

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
	log, buf := setupTestLogger(t)
	ctx := test.Context(t)
	instance, testRank := setupTestHarness(t, "1", log)
	mgr := setupTestManager(t, &config.Server{
		EngineAutoRestartMinDelay: 2, // 2 seconds for fast test
	}, log)

	// Record a recent restart
	mgr.lastRestart[testRank] = time.Now()

	req := engineRestartRequest{
		rank:     testRank,
		instance: instance,
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
	mgr.mu.Lock()
	if timer != nil {
		timer.Stop()
		delete(mgr.pendingRestart, testRank)
	}
	mgr.mu.Unlock()

	// Verify log message
	logOutput := buf.String()
	if !strings.Contains(logOutput, "rate limited") &&
		!strings.Contains(logOutput, "will restart in") {
		t.Error("expected rate limited message in log")
	}
}

func TestServer_EngineRestartManager_Stop(t *testing.T) {
	mgr := setupTestManager(t, nil)

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
	ctx, cancel := context.WithTimeout(test.Context(t), 10*time.Second)
	defer cancel()

	instance, testRank := setupTestHarness(t, "1")
	mgr := setupTestManager(t, &config.Server{
		EngineAutoRestartMinDelay: 10,
	})
	mgr.start(ctx)
	defer mgr.stop()

	startInstanceConsumer(ctx, instance)

	// Submit restart request
	mgr.requestRestart(testRank, instance)

	// Wait for restart time to be recorded
	if !waitForRestartRecorded(ctx, t, mgr, testRank) {
		t.Error("expected restart time to be recorded after processing")
	} else {
		t.Log("restart time recorded successfully")
	}
}

func TestServer_EngineRestartManager_DeferredRestartExecutes(t *testing.T) {
	ctx, cancel := context.WithTimeout(test.Context(t), 20*time.Second)
	defer cancel()

	instance, testRank := setupTestHarness(t, "1")
	mgr := setupTestManager(t, &config.Server{
		EngineAutoRestartMinDelay: 2, // seconds
	})

	startInstanceConsumer(ctx, instance)
	mgr.start(ctx)

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
	time.Sleep(5 * time.Second)

	// Verify timer was cleaned up
	mgr.mu.RLock()
	timer, exists = mgr.pendingRestart[testRank]
	mgr.mu.RUnlock()

	// Cleanup
	if timer != nil {
		timer.Stop()
	}

	if exists {
		t.Error("expected pending restart to be cleared after execution")
	}
}

func TestServer_EngineRestartManager_MultipleRanks(t *testing.T) {
	log, _ := setupTestLogger(t)
	ctx := test.Context(t)
	mgr := setupTestManager(t, &config.Server{
		EngineAutoRestartMinDelay: 2,
	}, log)

	instance1, rank1 := setupTestHarness(t, "1", log)
	instance2, rank2 := setupTestHarness(t, "2", log)

	startInstanceConsumer(ctx, instance1)
	startInstanceConsumer(ctx, instance2)
	mgr.start(ctx)

	// Request restarts for both ranks
	mgr.requestRestart(rank1, instance1)
	mgr.requestRestart(rank2, instance2)

	// Wait for both to complete
	if !waitForRestartRecorded(ctx, t, mgr, rank1) {
		t.Fatal("rank1 restart was not recorded")
	}
	if !waitForRestartRecorded(ctx, t, mgr, rank2) {
		t.Fatal("rank2 restart was not recorded")
	}

	// Verify both recorded
	mgr.mu.RLock()
	time1, exists1 := mgr.lastRestart[rank1]
	time2, exists2 := mgr.lastRestart[rank2]
	mgr.mu.RUnlock()

	if !exists1 || !exists2 {
		t.Fatal("expected both ranks to have restart times recorded")
	}

	// Verify rank1 was processed first or at the same time (not after)
	if time1.After(time2) {
		t.Error("expected rank1 restart time to not be after rank2")
	}

	// Both should be rate limited if requested again immediately
	mgr.requestRestart(rank1, instance1)
	mgr.requestRestart(rank2, instance2)

	// Wait for pending restarts to be scheduled
	if !waitForPendingRestart(ctx, t, mgr, rank1) {
		t.Fatal("rank1 pending restart was not scheduled")
	}
	if !waitForPendingRestart(ctx, t, mgr, rank2) {
		t.Fatal("rank2 pending restart was not scheduled")
	}

	// Verify both have pending restarts
	mgr.mu.RLock()
	_, pending1 := mgr.pendingRestart[rank1]
	_, pending2 := mgr.pendingRestart[rank2]
	mgr.mu.RUnlock()

	if !pending1 || !pending2 {
		t.Error("expected both ranks to have deferred restarts scheduled")
	}
}

func TestServer_EngineRestartManager_CancelExistingTimer(t *testing.T) {
	log, buf := setupTestLogger(t)
	ctx := test.Context(t)
	instance, testRank := setupTestHarness(t, "1", log)
	mgr := setupTestManager(t, &config.Server{
		EngineAutoRestartMinDelay: 5,
	}, log)

	// Set recent restart
	mgr.lastRestart[testRank] = time.Now()

	// First deferred request
	req1 := engineRestartRequest{
		rank:     testRank,
		instance: instance,
	}
	mgr.processRestartRequest(ctx, req1)

	mgr.mu.RLock()
	_, exists1 := mgr.pendingRestart[testRank]
	mgr.mu.RUnlock()

	if !exists1 {
		t.Fatal("expected first pending restart to be set")
	}

	// Second deferred request (should be dropped due to fast debounce)
	time.Sleep(100 * time.Millisecond)
	req2 := engineRestartRequest{
		rank:     testRank,
		instance: instance,
	}
	mgr.processRestartRequest(ctx, req2)

	// Verify log shows request was dropped
	logOutput := buf.String()
	if !strings.Contains(logOutput, "already has a deferred restart pending; dropping") {
		t.Error("expected debounce message in log")
	}

	// Cleanup
	mgr.mu.Lock()
	if timer, exists := mgr.pendingRestart[testRank]; exists {
		timer.Stop()
		delete(mgr.pendingRestart, testRank)
	}
	mgr.mu.Unlock()
}

func TestServer_NewEngineRestartManager(t *testing.T) {
	cfg := &config.Server{
		EngineAutoRestartMinDelay: 42,
	}
	ctx := test.MustLogContext(t)
	log := logging.FromContext(ctx)
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

func TestServer_EngineRestartManager_ClearRankRestartHistory(t *testing.T) {
	for name, tc := range map[string]struct {
		setupRanks     []ranklist.Rank
		clearRanks     []ranklist.Rank
		expectLogMsgs  []string
		remainingRanks []ranklist.Rank
	}{
		"nil manager": {
			setupRanks: []ranklist.Rank{1, 2},
			clearRanks: []ranklist.Rank{1},
		},
		"empty ranks": {
			setupRanks: []ranklist.Rank{1, 2},
			clearRanks: []ranklist.Rank{},
		},
		"clear single rank with history": {
			setupRanks:     []ranklist.Rank{1, 2, 3},
			clearRanks:     []ranklist.Rank{2},
			expectLogMsgs:  []string{"cleared restart history for rank 2"},
			remainingRanks: []ranklist.Rank{1, 3},
		},
		"clear multiple ranks with history": {
			setupRanks:     []ranklist.Rank{1, 2, 3, 4},
			clearRanks:     []ranklist.Rank{1, 3},
			expectLogMsgs:  []string{"cleared restart history for rank 1", "cleared restart history for rank 3"},
			remainingRanks: []ranklist.Rank{2, 4},
		},
		"clear all ranks": {
			setupRanks:     []ranklist.Rank{1, 2, 3},
			clearRanks:     []ranklist.Rank{1, 2, 3},
			expectLogMsgs:  []string{"cleared restart history for rank 1", "cleared restart history for rank 2", "cleared restart history for rank 3"},
			remainingRanks: []ranklist.Rank{},
		},
		"clear rank without history": {
			setupRanks:     []ranklist.Rank{1, 2},
			clearRanks:     []ranklist.Rank{5},
			expectLogMsgs:  []string{},
			remainingRanks: []ranklist.Rank{1, 2},
		},
		"clear rank with pending restart": {
			setupRanks:     []ranklist.Rank{1, 2},
			clearRanks:     []ranklist.Rank{1},
			expectLogMsgs:  []string{"cancelled pending restart for rank 1", "cleared restart history for rank 1"},
			remainingRanks: []ranklist.Rank{2},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := setupTestLogger(t)
			var mgr *engineRestartManager

			if name == "nil manager" {
				// Test nil manager doesn't panic
				var nilMgr *engineRestartManager
				nilMgr.clearRankRestartHistory(tc.clearRanks)
				return
			}

			mgr = setupTestManager(t, nil, log)

			// Setup restart history for ranks
			now := time.Now()
			for i, rank := range tc.setupRanks {
				mgr.lastRestart[rank] = now.Add(-time.Duration(i) * time.Minute)
			}

			// Setup pending restart for rank 1 if testing that case
			if name == "clear rank with pending restart" {
				timer := time.NewTimer(10 * time.Second)
				t.Cleanup(func() { timer.Stop() })
				mgr.pendingRestart[ranklist.Rank(1)] = timer
			}

			mgr.clearRankRestartHistory(tc.clearRanks)

			// Verify expected log messages
			for _, expectedMsg := range tc.expectLogMsgs {
				if !strings.Contains(buf.String(), expectedMsg) {
					t.Errorf("expected log message %q not found in: %s",
						expectedMsg, buf.String())
				}
			}

			// Verify remaining ranks still have history
			for _, rank := range tc.remainingRanks {
				if _, exists := mgr.lastRestart[rank]; !exists {
					t.Errorf("expected rank %d to still have restart history", rank)
				}
			}

			// Verify cleared ranks don't have history
			for _, rank := range tc.clearRanks {
				if _, exists := mgr.lastRestart[rank]; exists {
					found := false
					for _, remaining := range tc.remainingRanks {
						if remaining.Equals(rank) {
							found = true
							break
						}
					}
					if !found {
						t.Errorf("expected rank %d to have cleared restart history", rank)
					}
				}
			}

			// Verify pending restart was cleared for rank 1 in specific test
			if name == "clear rank with pending restart" {
				if _, exists := mgr.pendingRestart[ranklist.Rank(1)]; exists {
					t.Error("expected pending restart for rank 1 to be cleared")
				}
			}
		})
	}
}
