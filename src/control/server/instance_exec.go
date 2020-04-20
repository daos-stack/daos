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

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

// IOServerRunner defines an interface for starting and stopping the
// daos_io_server.
type IOServerRunner interface {
	Start(context.Context, chan<- error) error
	IsRunning() bool
	Signal(os.Signal) error
	Wait() error
	GetConfig() *ioserver.Config
}

// IsStarted indicates whether IOServerInstance is in a running state.
func (srv *IOServerInstance) IsStarted() bool {
	return srv.runner.IsRunning()
}

func (srv *IOServerInstance) format(ctx context.Context, recreateSBs bool) (err error) {
	if err = srv.awaitStorageReady(ctx, recreateSBs); err != nil {
		return
	}
	if err = srv.createSuperblock(recreateSBs); err != nil {
		return
	}

	return
}

// start checks to make sure that the instance has a valid superblock before
// performing any required NVMe preparation steps and launching a managed
// daos_io_server instance.
func (srv *IOServerInstance) start(ctx context.Context, errChan chan<- error) error {
	if !srv.hasSuperblock() {
		if err := srv.ReadSuperblock(); err != nil {
			return errors.Wrap(err, "start failed; no superblock")
		}
	}
	if err := srv.bdevClassProvider.PrepareDevices(); err != nil {
		return errors.Wrap(err, "start failed; unable to prepare NVMe device(s)")
	}
	if err := srv.bdevClassProvider.GenConfigFile(); err != nil {
		return errors.Wrap(err, "start failed; unable to generate NVMe configuration for SPDK")
	}

	if err := srv.logScmStorage(); err != nil {
		srv.log.Errorf("instance %d: unable to log SCM storage stats: %s", srv.Index(), err)
	}

	return srv.runner.Start(ctx, errChan)
}

// waitReady awaits ready signal from I/O server before starting
// management service on MS replicas immediately so other instances can join.
// I/O server modules are then loaded.
func (srv *IOServerInstance) waitReady(ctx context.Context, errChan chan error) error {
	srv.log.Debugf("waiting for instance %d to start-up", srv.Index())

	select {
	case <-ctx.Done(): // propagated harness exit
		return ctx.Err()
	case err := <-errChan:
		// TODO: Restart failed instances on unexpected exit.
		return errors.Wrapf(err, "instance %d exited prematurely", srv.Index())
	case ready := <-srv.AwaitDrpcReady():
		if err := srv.finishStartup(ctx, ready); err != nil {
			return err
		}
	}

	return nil
}

// finishStartup sets up instance once dRPC comms are ready, this includes
// setting the instance rank, starting management service and loading IO server
// modules.
//
// Instance ready state is set to indicate that all setup is complete.
func (srv *IOServerInstance) finishStartup(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	if err := srv.SetRank(ctx, ready); err != nil {
		return err
	}
	// update ioserver target count to reflect allocated
	// number of targets, not number requested when starting
	srv.SetTargetCount(int(ready.GetNtgts()))

	if srv.IsMSReplica() {
		if err := srv.StartManagementService(); err != nil {
			return errors.Wrap(err, "failed to start management service")
		}
	}

	if err := srv.LoadModules(); err != nil {
		return errors.Wrap(err, "failed to load I/O server modules")
	}

	srv.ready.SetTrue()

	return nil
}

func (srv *IOServerInstance) exit(exitErr error) {
	srv.log.Infof("instance %d exited: %s", srv.Index(), exitErr)

	srv._lastErr = exitErr
	if err := srv.RemoveSocket(); err != nil {
		srv.log.Errorf("removing socket file: %s", err)
	}
}

func (srv *IOServerInstance) run(ctx context.Context, errOut chan error, membership *system.Membership, recreateSBs bool) (err error) {
	if err = srv.format(ctx, recreateSBs); err != nil {
		return
	}

	if err = srv.start(ctx, errOut); err != nil {
		return
	}
	if srv.IsMSReplica() {
		// MS bootstrap will not join so register manually
		if err := srv.registerMember(membership); err != nil {
			return err
		}
	}
	srv.waitDrpc.SetTrue()

	if err = srv.waitReady(ctx, errOut); err != nil {
		return
	}

	return
}

// Start is the main processing loop for an IOServerInstance.
func (srv *IOServerInstance) Start(parent context.Context, membership *system.Membership, cfg *Configuration) {
	ctx, cancel := context.WithCancel(parent)
	defer cancel()

	errChan := make(chan error)

	// spawn instance processing loops and listen for exit
	go func(ctxIn context.Context) {
		errChan <- srv.run(ctxIn, errChan, membership, cfg.RecreateSuperblocks)
	}(ctx)

	// error will either be received from instance runner or one of the
	// setup routines in run()
	srv.exit(<-errChan)
}

// Stop sends signal to stop IOServerInstance runner (but doesn't wait for
// process to exit).
func (srv *IOServerInstance) Stop(signal os.Signal) error {
	if err := srv.runner.Signal(signal); err != nil {
		return err
	}

	return nil
}
