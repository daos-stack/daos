//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/engine"
)

// EngineRunner defines an interface for starting and stopping the
// daos_engine.
type EngineRunner interface {
	Start(context.Context) (engine.RunnerExitChan, error)
	IsRunning() bool
	Signal(os.Signal)
	GetConfig() *engine.Config
}

func (ei *EngineInstance) format(ctx context.Context, recreateSBs bool) error {
	idx := ei.Index()

	ei.log.Debugf("instance %d: checking if storage is formatted", idx)
	if err := ei.awaitStorageReady(ctx, recreateSBs); err != nil {
		return err
	}
	if err := ei.createSuperblock(recreateSBs); err != nil {
		return err
	}

	if !ei.hasSuperblock() {
		return errors.Errorf("instance %d: no superblock after format", idx)
	}

	// After we know that the instance storage is ready, fire off
	// any callbacks that were waiting for this state.
	for _, readyFn := range ei.onStorageReady {
		if err := readyFn(ctx); err != nil {
			return err
		}
	}

	return nil
}

// start checks to make sure that the instance has a valid superblock before
// performing any required NVMe preparation steps and launching a managed
// daos_engine instance.
func (ei *EngineInstance) start(ctx context.Context) (chan *engine.RunnerExitInfo, error) {
	if err := ei.logScmStorage(); err != nil {
		ei.log.Errorf("instance %d: unable to log SCM storage stats: %s", ei.Index(), err)
	}

	return ei.runner.Start(ctx)
}

// waitReady awaits ready signal from I/O Engine before starting
// management service on MS replicas immediately so other instances can join.
// I/O Engine modules are then loaded.
func (ei *EngineInstance) waitReady(ctx context.Context) error {
	select {
	case <-ctx.Done(): // propagated harness exit
		return ctx.Err()
	case ready := <-ei.awaitDrpcReady():
		if err := ei.finishStartup(ctx, ready); err != nil {
			return err
		}
		return nil
	}
}

// finishStartup sets up instance once dRPC comms are ready, this includes setting the instance
// rank, starting management service and loading I/O Engine modules.
//
// Instance ready state is set to indicate that all setup is complete.
func (ei *EngineInstance) finishStartup(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	if err := ei.handleReady(ctx, ready); err != nil {
		return err
	}

	ei.ready.SetTrue()

	for _, fn := range ei.onReady {
		if err := fn(ctx); err != nil {
			return err
		}
	}

	return nil
}

// createPublishInstanceExitFunc returns onInstanceExitFn which will publish an exit
// event using the provided publish function.
func createPublishInstanceExitFunc(publish func(*events.RASEvent), hostname string) onInstanceExitFn {
	return func(_ context.Context, engineIdx uint32, rank ranklist.Rank, exitErr error, exPid int) error {
		if exitErr == nil {
			return errors.New("expected non-nil exit error")
		}

		evt := events.NewEngineDiedEvent(hostname, engineIdx, rank.Uint32(),
			common.ExitStatus(exitErr.Error()), exPid)

		// set forwardable if there is a rank for the MS to operate on
		publish(evt.WithForwardable(!rank.Equals(ranklist.NilRank)))

		return nil
	}
}

func (ei *EngineInstance) handleExit(ctx context.Context, exitPid int, exitErr error) {
	engineIdx := ei.Index()
	rank, err := ei.GetRank()
	if err != nil {
		ei.log.Debugf("instance %d: no rank (%s)", engineIdx, err)
	}

	ei._lastErr = exitErr

	details := []string{fmt.Sprintf("instance %d", engineIdx)}
	if exitPid != 0 {
		details = append(details, fmt.Sprintf("pid %d", exitPid))
	}
	if !rank.Equals(ranklist.NilRank) {
		details = append(details, fmt.Sprintf("rank %d", rank))
	}
	strDetails := strings.Join(details, ", ")

	ei.log.Infof("%s exited with status: %s", strDetails, common.GetExitStatus(exitErr))

	// After we know that the instance has exited, fire off
	// any callbacks that were waiting for this state.
	for _, exitFn := range ei.onInstanceExit {
		err := exitFn(ctx, engineIdx, rank, exitErr, exitPid)
		if err != nil {
			ei.log.Errorf("onExit: %s", err)
		}
	}

	if err := ei.removeSocket(); err != nil && !os.IsNotExist(errors.Cause(err)) {
		ei.log.Errorf("removing socket file: %s", err)
	}
}

// startRunner performs setup of and starts process runner for I/O Engine instance and
// will only return (if no errors are returned during setup) on I/O Engine
// process exit (triggered by harness shutdown through context cancellation
// or abnormal I/O Engine process termination).
func (ei *EngineInstance) startRunner(parent context.Context, recreateSBs bool) (_ chan *engine.RunnerExitInfo, err error) {
	ctx, cancel := context.WithCancel(parent)
	defer func() {
		if err != nil {
			// If there was an error, cancel the context to signal the
			// cleanup handlers.
			cancel()
		}
	}()

	if err = ei.format(ctx, recreateSBs); err != nil {
		return
	}

	runnerExitChan, err := ei.start(ctx)
	if err != nil {
		return
	}
	ei.waitDrpc.SetTrue()

	return runnerExitChan, ei.waitReady(ctx)
}

// requestStart makes a request to (re-)start the engine, and blocks
// until the request is received.
func (ei *EngineInstance) requestStart(ctx context.Context) {
	select {
	case <-ctx.Done():
	case ei.startRequested <- true:
	}
}

// Run starts the control loop for an EngineInstance. Engine starts are triggered by
// calling requestStart() on the instance.
func (ei *EngineInstance) Run(ctx context.Context, recreateSBs bool) {
	// Start the instance control loop.
	go func() {
		var runnerExitCh engine.RunnerExitChan
		var err error
		var restartRequested bool
		for {
			select {
			case <-ctx.Done():
				return
			case relaunch := <-ei.startRequested:
				if !relaunch {
					return
				}

				if runnerExitCh != nil {
					restartRequested = true
					continue
				}

				runnerExitCh, err = ei.startRunner(ctx, recreateSBs)
				if err != nil {
					ei.log.Errorf("runner exited without starting process: %s", err)
					ei.handleExit(ctx, 0, err)
					continue
				}
			case runnerExit := <-runnerExitCh:
				ei.handleExit(ctx, runnerExit.PID, runnerExit.Error)
				runnerExitCh = nil // next runner will reset this
				if restartRequested {
					go ei.requestStart(ctx)
					restartRequested = false
				}
			}
		}
	}()

	// Start the instance runner.
	ei.requestStart(ctx)
}

// Stop sends signal to stop EngineInstance runner (nonblocking).
func (ei *EngineInstance) Stop(signal os.Signal) error {
	ei.runner.Signal(signal)
	return nil
}
