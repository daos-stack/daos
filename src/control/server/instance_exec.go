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

	"github.com/daos-stack/daos/src/control/build"
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

func (srv *EngineInstance) format(ctx context.Context, recreateSBs bool) error {
	idx := srv.Index()

	srv.log.Debugf("instance %d: checking if storage is formatted", idx)
	if err := srv.awaitStorageReady(ctx, recreateSBs); err != nil {
		return err
	}
	if err := srv.createSuperblock(recreateSBs); err != nil {
		return err
	}

	if !srv.hasSuperblock() {
		return errors.Errorf("instance %d: no superblock after format", idx)
	}

	// After we know that the instance storage is ready, fire off
	// any callbacks that were waiting for this state.
	for _, readyFn := range srv.onStorageReady {
		if err := readyFn(ctx); err != nil {
			return err
		}
	}

	return nil
}

// start checks to make sure that the instance has a valid superblock before
// performing any required NVMe preparation steps and launching a managed
// daos_engine instance.
func (srv *EngineInstance) start(ctx context.Context, errChan chan<- error) error {
	srv.log.Debug("instance start()")
	if err := srv.bdevClassProvider.GenConfigFile(); err != nil {
		return errors.Wrap(err, "start failed; unable to generate NVMe configuration for SPDK")
	}

	if err := srv.logScmStorage(); err != nil {
		srv.log.Errorf("instance %d: unable to log SCM storage stats: %s", srv.Index(), err)
	}

	// async call returns immediately, runner sends on errChan when ctx.Done()
	return srv.runner.Start(ctx, errChan)
}

// waitReady awaits ready signal from I/O Engine before starting
// management service on MS replicas immediately so other instances can join.
// I/O Engine modules are then loaded.
func (srv *EngineInstance) waitReady(ctx context.Context, errChan chan error) error {
	srv.log.Debugf("instance %d: awaiting %s init", srv.Index(), build.DataPlaneName)

	select {
	case <-ctx.Done(): // propagated harness exit
		return ctx.Err()
	case err := <-errChan:
		// TODO: Restart failed instances on unexpected exit.
		return errors.Wrapf(err, "instance %d exited prematurely", srv.Index())
	case ready := <-srv.awaitDrpcReady():
		if err := srv.finishStartup(ctx, ready); err != nil {
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
func (srv *EngineInstance) finishStartup(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	if err := srv.handleReady(ctx, ready); err != nil {
		return err
	}
	// update engine target count to reflect allocated
	// number of targets, not number requested when starting
	srv.setTargetCount(int(ready.GetNtgts()))

	srv.ready.SetTrue()

	for _, fn := range srv.onReady {
		if err := fn(ctx); err != nil {
			return err
		}
	}

	return nil
}

// publishInstanceExitFn returns onInstanceExitFn which will publish an exit
// event using the provided publish function.
func publishInstanceExitFn(publishFn func(*events.RASEvent), hostname string, srvIdx uint32) onInstanceExitFn {
	return func(_ context.Context, rank system.Rank, exitErr error) error {
		if exitErr == nil {
			return errors.New("expected non-nil exit error")
		}

		evt := events.NewRankDownEvent(hostname, srvIdx, rank.Uint32(),
			common.ExitStatus(exitErr.Error()))

		// set forwardable if there is a rank for the MS to operate on
		publishFn(evt.WithForwardable(!rank.Equals(system.NilRank)))

		return nil
	}
}

func (srv *EngineInstance) exit(ctx context.Context, exitErr error) {
	srvIdx := srv.Index()

	srv.log.Infof("instance %d exited: %s", srvIdx, common.GetExitStatus(exitErr))

	rank, err := srv.GetRank()
	if err != nil {
		srv.log.Debugf("instance %d: no rank (%s)", srv.Index(), err)
	}

	srv._lastErr = exitErr
	if err := srv.removeSocket(); err != nil {
		srv.log.Errorf("removing socket file: %s", err)
	}

	// After we know that the instance has exited, fire off
	// any callbacks that were waiting for this state.
	for _, exitFn := range srv.onInstanceExit {
		if err := exitFn(ctx, rank, exitErr); err != nil {
			srv.log.Errorf("onExit: %s", err)
		}
	}
}

// run performs setup of and starts process runner for I/O Engine instance and
// will only return (if no errors are returned during setup) on I/O Engine
// process exit (triggered by harness shutdown through context cancellation
// or abnormal I/O Engine process termination).
func (srv *EngineInstance) run(ctx context.Context, recreateSBs bool) (err error) {
	errChan := make(chan error)

	if err = srv.format(ctx, recreateSBs); err != nil {
		return
	}

	if err = srv.start(ctx, errChan); err != nil {
		return
	}
	srv.waitDrpc.SetTrue()

	if err = srv.waitReady(ctx, errChan); err != nil {
		return
	}

	return <-errChan // receive on runner exit
}

// Run is the processing loop for an EngineInstance. Starts are triggered by
// receiving true on instance start channel.
func (srv *EngineInstance) Run(ctx context.Context, recreateSBs bool) {
	for {
		select {
		case <-ctx.Done():
			return
		case relaunch := <-srv.startLoop:
			if !relaunch {
				return
			}
			srv.exit(ctx, srv.run(ctx, recreateSBs))
		}
	}
}

// Stop sends signal to stop EngineInstance runner (nonblocking).
func (srv *EngineInstance) Stop(signal os.Signal) error {
	if err := srv.runner.Signal(signal); err != nil {
		return err
	}

	return nil
}
