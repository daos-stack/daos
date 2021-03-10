//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/system"
)

// EngineRunner defines an interface for starting and stopping the
// daos_engine.
type EngineRunner interface {
	Start(context.Context, chan<- error) error
	IsRunning() bool
	Signal(os.Signal) error
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
func (ei *EngineInstance) start(ctx context.Context, errChan chan<- error) error {
	if err := ei.bdevClassProvider.GenConfigFile(); err != nil {
		return errors.Wrap(err, "start failed; unable to generate NVMe configuration for SPDK")
	}

	if err := ei.logScmStorage(); err != nil {
		ei.log.Errorf("instance %d: unable to log SCM storage stats: %s", ei.Index(), err)
	}

	// async call returns immediately, runner sends on errChan when ctx.Done()
	return ei.runner.Start(ctx, errChan)
}

// waitReady awaits ready signal from I/O Engine before starting
// management service on MS replicas immediately so other instances can join.
// I/O Engine modules are then loaded.
func (ei *EngineInstance) waitReady(ctx context.Context, errChan chan error) error {
	select {
	case <-ctx.Done(): // propagated harness exit
		return ctx.Err()
	case err := <-errChan:
		// TODO: Restart failed instances on unexpected exit.
		return errors.Wrapf(err, "instance %d exited prematurely", ei.Index())
	case ready := <-ei.awaitDrpcReady():
		if err := ei.finishStartup(ctx, ready); err != nil {
			return err
		}
		return nil
	}
}

// finishStartup sets up instance once dRPC comms are ready, this includes
// setting the instance rank, starting management service and loading I/O Engine
// modules.
//
// Instance ready state is set to indicate that all setup is complete.
func (ei *EngineInstance) finishStartup(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	if err := ei.handleReady(ctx, ready); err != nil {
		return err
	}
	// update engine target count to reflect allocated
	// number of targets, not number requested when starting
	ei.setTargetCount(int(ready.GetNtgts()))

	ei.ready.SetTrue()

	for _, fn := range ei.onReady {
		if err := fn(ctx); err != nil {
			return err
		}
	}

	return nil
}

// publishInstanceExitFn returns onInstanceExitFn which will publish an exit
// event using the provided publish function.
func publishInstanceExitFn(publishFn func(*events.RASEvent), hostname string, engineIdx uint32) onInstanceExitFn {
	return func(_ context.Context, rank system.Rank, exitErr error) error {
		if exitErr == nil {
			return errors.New("expected non-nil exit error")
		}

		evt := events.NewRankDownEvent(hostname, engineIdx, rank.Uint32(),
			common.ExitStatus(exitErr.Error()))

		// set forwardable if there is a rank for the MS to operate on
		publishFn(evt.WithForwardable(!rank.Equals(system.NilRank)))

		return nil
	}
}

func (ei *EngineInstance) exit(ctx context.Context, exitErr error) {
	engineIdx := ei.Index()

	ei.log.Infof("instance %d exited: %s", engineIdx, common.GetExitStatus(exitErr))

	rank, err := ei.GetRank()
	if err != nil {
		ei.log.Debugf("instance %d: no rank (%s)", ei.Index(), err)
	}

	ei._lastErr = exitErr
	if err := ei.removeSocket(); err != nil {
		ei.log.Errorf("removing socket file: %s", err)
	}

	// After we know that the instance has exited, fire off
	// any callbacks that were waiting for this state.
	for _, exitFn := range ei.onInstanceExit {
		if err := exitFn(ctx, rank, exitErr); err != nil {
			ei.log.Errorf("onExit: %s", err)
		}
	}
}

// run performs setup of and starts process runner for I/O Engine instance and
// will only return (if no errors are returned during setup) on I/O Engine
// process exit (triggered by harness shutdown through context cancellation
// or abnormal I/O Engine process termination).
func (ei *EngineInstance) run(ctx context.Context, recreateSBs bool) (err error) {
	errChan := make(chan error)

	if err = ei.format(ctx, recreateSBs); err != nil {
		return
	}

	if err = ei.start(ctx, errChan); err != nil {
		return
	}
	ei.waitDrpc.SetTrue()

	if err = ei.waitReady(ctx, errChan); err != nil {
		return
	}

	return <-errChan // receive on runner exit
}

// Run is the processing loop for an EngineInstance. Starts are triggered by
// receiving true on instance start channel.
func (ei *EngineInstance) Run(ctx context.Context, recreateSBs bool) {
	for {
		select {
		case <-ctx.Done():
			return
		case relaunch := <-ei.startLoop:
			if !relaunch {
				return
			}
			ei.exit(ctx, ei.run(ctx, recreateSBs))
		}
	}
}

// Stop sends signal to stop EngineInstance runner (nonblocking).
func (ei *EngineInstance) Stop(signal os.Signal) error {
	if err := ei.runner.Signal(signal); err != nil {
		return err
	}

	return nil
}
