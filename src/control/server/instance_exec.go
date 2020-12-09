//
// (C) Copyright 2020 Intel Corporation.
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
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

// IOServerRunner defines an interface for starting and stopping the
// daos_io_server.
type IOServerRunner interface {
	Start(context.Context, chan<- error) error
	IsRunning() bool
	Signal(os.Signal) error
	GetConfig() *ioserver.Config
}

func (srv *IOServerInstance) format(ctx context.Context, recreateSBs bool) error {
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
// daos_io_server instance.
func (srv *IOServerInstance) start(ctx context.Context, errChan chan<- error) error {
	if err := srv.bdevClassProvider.GenConfigFile(); err != nil {
		return errors.Wrap(err, "start failed; unable to generate NVMe configuration for SPDK")
	}

	if err := srv.logScmStorage(); err != nil {
		srv.log.Errorf("instance %d: unable to log SCM storage stats: %s", srv.Index(), err)
	}

	// async call returns immediately, runner sends on errChan when ctx.Done()
	return srv.runner.Start(ctx, errChan)
}

// waitReady awaits ready signal from I/O server before starting
// management service on MS replicas immediately so other instances can join.
// I/O server modules are then loaded.
func (srv *IOServerInstance) waitReady(ctx context.Context, errChan chan error) error {
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
// setting the instance rank, starting management service and loading IO server
// modules.
//
// Instance ready state is set to indicate that all setup is complete.
func (srv *IOServerInstance) finishStartup(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	if err := srv.handleReady(ctx, ready); err != nil {
		return err
	}
	// update ioserver target count to reflect allocated
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

func (srv *IOServerInstance) exit(exitErr error) {
	srv.log.Infof("instance %d exited: %s", srv.Index(),
		ioserver.GetExitStatus(exitErr))

	srv._lastErr = exitErr
	if err := srv.removeSocket(); err != nil {
		srv.log.Errorf("removing socket file: %s", err)
	}
}

// run performs setup of and starts process runner for IO server instance and
// will only return (if no errors are returned during setup) on IO server
// process exit (triggered by harness shutdown through context cancellation
// or abnormal IO server process termination).
func (srv *IOServerInstance) run(ctx context.Context, recreateSBs bool) (err error) {
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

// Run is the processing loop for an IOServerInstance. Starts are triggered by
// receiving true on instance start channel.
func (srv *IOServerInstance) Run(ctx context.Context, recreateSBs bool) {
	for {
		select {
		case <-ctx.Done():
			return
		case relaunch := <-srv.startLoop:
			if !relaunch {
				return
			}
			srv.exit(srv.run(ctx, recreateSBs))
		}
	}
}

// Stop sends signal to stop IOServerInstance runner (nonblocking).
func (srv *IOServerInstance) Stop(signal os.Signal) error {
	if err := srv.runner.Signal(signal); err != nil {
		return err
	}

	return nil
}
