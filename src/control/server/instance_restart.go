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

// waitForEngineStopped polls until the engine instance is stopped.
func waitForEngineStopped(ctx context.Context, instances []Engine) error {
	pollFn := func(e Engine) bool { return !e.IsStarted() }
	return pollInstanceState(ctx, instances, pollFn)
}

// processRestartRequest handles a single restart request with rate limiting.
func (mgr *engineRestartManager) processRestartRequest(ctx context.Context, req engineRestartRequest) {
	rank, instance := req.rank, req.instance

	mgr.mu.Lock()
	if last, ok := mgr.lastRestart[rank]; ok {
		if elapsed := time.Since(last); elapsed < mgr.getMinDelay() {
			// Fast debounce for subsequent requests inside of the delay window
			if _, pending := mgr.pendingRestart[rank]; pending {
				mgr.mu.Unlock()
				mgr.log.Debugf("rank %d already has a deferred restart pending; dropping",
					rank)
				return
			}

			// First restart request inside of the delay window claims it
			remaining := mgr.getMinDelay() - elapsed
			mgr.pendingRestart[rank] = time.AfterFunc(remaining, func() {
				mgr.requestRestart(rank, instance)
			})
			mgr.mu.Unlock()
			mgr.log.Noticef("rank %d restart rate limited: will restart in %s",
				rank, remaining.Round(time.Second))
			return
		}
	}

	// If this is the first restart or it's outside of the delay window, start the process (over)
	mgr.lastRestart[rank] = time.Now()
	delete(mgr.pendingRestart, rank)
	mgr.mu.Unlock()

	if err := waitForEngineStopped(ctx, []Engine{instance}); err != nil {
		mgr.log.Errorf("rank %d did not stop before restart: %s", rank, err)
		return
	}
	mgr.log.Noticef("restart manager is restarting rank %d", rank)
	instance.requestStart(ctx)
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

// start begins processing restart requests. Function to be called once on server start-up.
func (mgr *engineRestartManager) start(ctx context.Context) {
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

// stop shuts down the restart manager. Function to be called once on server shutdown.
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

	close(mgr.stopChan)
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
