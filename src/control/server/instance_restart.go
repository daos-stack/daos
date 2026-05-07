//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"sync"
	"time"

	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

const (
	// engineRestartMaxQueueSz is the maximum number of engine restart requests to be held in a
	// channel at any one time. Additional requests will be dropped during exclusion storm.
	engineRestartMaxQueueSz = 100

	// defaultEngineAutoRestartMinDelay is the minimum number of seconds between automatic engine
	// restarts that are triggered when engine_self_terminated RAS events are received.
	defaultEngineAutoRestartMinDelay = 300 // 5 minutes
)

// engineRestartRequest represents a request to restart an engine instance.
type engineRestartRequest struct {
	rank     ranklist.Rank
	instance Engine
}

// engineRestartManager manages engine restart requests with rate limiting.
type engineRestartManager struct {
	log            logging.Logger
	cfg            *config.Server
	requestChan    chan engineRestartRequest
	stopChan       chan struct{}
	lastRestart    map[ranklist.Rank]time.Time
	pendingRestart map[ranklist.Rank]*time.Timer
	mu             sync.RWMutex
}

// getMinDelay returns the configured minimum delay between restarts.
func (mgr *engineRestartManager) getMinDelay() time.Duration {
	minDelay := defaultEngineAutoRestartMinDelay
	if mgr.cfg.EngineAutoRestartMinDelay > 0 {
		minDelay = mgr.cfg.EngineAutoRestartMinDelay
	}
	return time.Duration(minDelay) * time.Second
}

// canRestartNow checks if a rank can be restarted immediately.
// Returns true if restart can proceed, false and delay duration if rate limited.
func (mgr *engineRestartManager) canRestartNow(rank ranklist.Rank) (bool, time.Duration) {
	mgr.mu.RLock()
	defer mgr.mu.RUnlock()

	lastRestart, hasRestarted := mgr.lastRestart[rank]
	if !hasRestarted {
		return true, 0
	}

	minDelay := mgr.getMinDelay()
	elapsed := time.Since(lastRestart)
	if elapsed >= minDelay {
		return true, 0
	}

	remaining := minDelay - elapsed
	return false, remaining
}

// recordRestartTime records when a rank was restarted.
func (mgr *engineRestartManager) recordRestartTime(rank ranklist.Rank) {
	mgr.mu.Lock()
	defer mgr.mu.Unlock()

	mgr.lastRestart[rank] = time.Now()
	mgr.log.Debugf("last restart recorded")
}

// clearPendingRestart removes a pending restart timer for a rank.
func (mgr *engineRestartManager) clearPendingRestart(rank ranklist.Rank) {
	mgr.mu.Lock()
	defer mgr.mu.Unlock()

	delete(mgr.pendingRestart, rank)
}

// setPendingRestart stores a pending restart timer for a rank.
func (mgr *engineRestartManager) setPendingRestart(rank ranklist.Rank, timer *time.Timer) {
	mgr.mu.Lock()
	defer mgr.mu.Unlock()

	// Cancel any existing timer
	if existingTimer, exists := mgr.pendingRestart[rank]; exists {
		existingTimer.Stop()
		mgr.log.Debugf("cancelled existing pending restart timer for rank %d", rank)
	}

	mgr.pendingRestart[rank] = timer
}

// waitForEngineStopped polls until the engine instance is stopped.
func waitForEngineStopped(ctx context.Context, instances []Engine) error {
	pollFn := func(e Engine) bool { return !e.IsStarted() }
	return pollInstanceState(ctx, instances, pollFn)
}

// performRestart executes the restart after waiting for the engine to stop.
func (mgr *engineRestartManager) performRestart(ctx context.Context, rank ranklist.Rank, instance Engine) {
	defer mgr.clearPendingRestart(rank)

	// Wait for engine to stop
	instances := []Engine{instance}
	if err := waitForEngineStopped(ctx, instances); err != nil {
		mgr.log.Errorf("rank %d did not stop before restart: %s", rank, err)
		return
	}

	mgr.log.Noticef("restarting rank %d", rank)
	instance.requestStart(ctx)

	// Record restart time and clear pending state on exit (deferred)
	mgr.recordRestartTime(rank)
	mgr.log.Debugf("recording rank %d", rank)
}

// processRestartRequest handles a single restart request with rate limiting.
func (mgr *engineRestartManager) processRestartRequest(ctx context.Context, req engineRestartRequest) {
	rank := req.rank
	instance := req.instance

	mgr.log.Debugf("processing restart request for rank %d", rank)

	canRestart, delay := mgr.canRestartNow(rank)
	if !canRestart {
		mgr.log.Noticef("rank %d restart rate limited: will restart in %s",
			rank, delay.Round(time.Second))

		// Schedule deferred restart
		timer := time.AfterFunc(delay, func() {
			mgr.log.Noticef("deferred restart triggered for rank %d after rate-limit delay", rank)
			mgr.performRestart(ctx, rank, instance)
		})

		// Overwrite any existing pending restart
		mgr.setPendingRestart(rank, timer)
		return
	}

	// Can restart immediately
	mgr.performRestart(ctx, rank, instance)
}

// requestRestart submits a restart request to the manager.
func (mgr *engineRestartManager) requestRestart(rank ranklist.Rank, instance Engine) {
	req := engineRestartRequest{
		rank:     rank,
		instance: instance,
	}

	select {
	case mgr.requestChan <- req:
		mgr.log.Debugf("restart request queued for rank %d", rank)
	default:
		mgr.log.Errorf("restart request channel full, dropping request for rank %d", rank)
	}
}

// start begins processing restart requests.
func (mgr *engineRestartManager) start(ctx context.Context) {
	mgr.mu.Lock()
	// Reinitialize channels if they were closed
	if mgr.stopChan == nil {
		mgr.stopChan = make(chan struct{})
	}
	if mgr.requestChan == nil {
		mgr.requestChan = make(chan engineRestartRequest, engineRestartMaxQueueSz)
	}
	mgr.mu.Unlock()

	mgr.log.Debug("engine restart manager started")
	go func() {
		for {
			select {
			case <-ctx.Done():
				mgr.log.Debug("engine restart manager context cancelled")
				return
			case <-mgr.stopChan:
				mgr.log.Debug("engine restart manager stopped")
				return
			case req := <-mgr.requestChan:
				mgr.processRestartRequest(ctx, req)
			}
		}
	}()
}

// clearRankRestartHistory clears the restart history for specific ranks.
// This is called when ranks are manually stopped or started to ensure
// manual operations don't interfere with automatic restart rate limiting.
func (mgr *engineRestartManager) clearRankRestartHistory(ranks []ranklist.Rank) {
	if mgr == nil || len(ranks) == 0 {
		return
	}

	mgr.mu.Lock()
	defer mgr.mu.Unlock()

	for _, rank := range ranks {
		// Cancel any pending restart for this rank
		if timer, exists := mgr.pendingRestart[rank]; exists {
			timer.Stop()
			delete(mgr.pendingRestart, rank)
			mgr.log.Debugf("cancelled pending restart for rank %d during manual operation", rank)
		}

		// Clear restart history for this rank
		if _, exists := mgr.lastRestart[rank]; exists {
			delete(mgr.lastRestart, rank)
			mgr.log.Debugf("cleared restart history for rank %d (manual operation)", rank)
		}
	}
}

// stop shuts down the restart manager.
func (mgr *engineRestartManager) stop() {
	mgr.log.Debug("stopping engine restart manager")
	mgr.mu.Lock()
	defer mgr.mu.Unlock()

	// Cancel all pending restart timers
	for rank, timer := range mgr.pendingRestart {
		timer.Stop()
		mgr.log.Debugf("cancelled pending restart for rank %d", rank)
	}
	mgr.pendingRestart = make(map[ranklist.Rank]*time.Timer)

	// Close stopChan if it's open
	if mgr.stopChan != nil {
		close(mgr.stopChan)
		mgr.stopChan = nil
	}
}

// newEngineRestartManager creates a new restart manager.
func newEngineRestartManager(log logging.Logger, cfg *config.Server) *engineRestartManager {
	return &engineRestartManager{
		log:            log,
		cfg:            cfg,
		requestChan:    make(chan engineRestartRequest, engineRestartMaxQueueSz),
		stopChan:       make(chan struct{}),
		lastRestart:    make(map[ranklist.Rank]time.Time),
		pendingRestart: make(map[ranklist.Rank]*time.Timer),
	}
}
