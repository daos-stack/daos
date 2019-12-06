//
// (C) Copyright 2019 Intel Corporation.
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
	"fmt"
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// IOServerHarness is responsible for managing IOServer instances
type IOServerHarness struct {
	sync.RWMutex
	log              logging.Logger
	instances        []*IOServerInstance
	started          bool
	restartInstances chan struct{}
	errChan          chan error
}

// NewHarness returns an initialized *IOServerHarness
func NewIOServerHarness(log logging.Logger) *IOServerHarness {
	return &IOServerHarness{
		log:              log,
		instances:        make([]*IOServerInstance, 0, 2),
		restartInstances: make(chan struct{}, 1),
		errChan:          make(chan error, 2),
	}
}

func (h *IOServerHarness) IsStarted() bool {
	h.RLock()
	defer h.RUnlock()
	return h.started
}

func (h *IOServerHarness) Instances() []*IOServerInstance {
	h.RLock()
	defer h.RUnlock()
	return h.instances
}

// AddInstance adds a new IOServer instance to be managed.
func (h *IOServerHarness) AddInstance(srv *IOServerInstance) error {
	if h.IsStarted() {
		return errors.New("can't add instance to already-started harness")
	}

	h.Lock()
	defer h.Unlock()
	srv.SetIndex(uint32(len(h.instances)))

	h.instances = append(h.instances, srv)
	return nil
}

// GetMSLeaderInstance returns a managed IO Server instance to be used as a
// management target and fails if selected instance is not MS Leader.
func (h *IOServerHarness) GetMSLeaderInstance() (*IOServerInstance, error) {
	h.RLock()
	defer h.RUnlock()

	if len(h.instances) == 0 {
		return nil, errors.New("harness has no managed instances")
	}

	var err error
	for _, mi := range h.Instances() {
		// try each instance, returning the first one that is a replica (if any are)
		if err = checkIsMSReplica(mi); err == nil {
			return mi, nil
		}
	}

	return nil, err
}

// CreateSuperblocks creates instance superblocks as needed.
func (h *IOServerHarness) CreateSuperblocks(recreate bool) error {
	if h.IsStarted() {
		return errors.Errorf("Can't create superblocks with running instances")
	}

	instances := h.Instances()
	toCreate := make([]*IOServerInstance, 0, len(instances))

	for _, instance := range instances {
		needsSuperblock, err := instance.NeedsSuperblock()
		if !needsSuperblock {
			continue
		}
		if err != nil && !recreate {
			return err
		}
		toCreate = append(toCreate, instance)
	}

	if len(toCreate) == 0 {
		return nil
	}

	for _, instance := range toCreate {
		// Only the first I/O server can be an MS replica.
		if instance.Index() == 0 {
			mInfo, err := getMgmtInfo(instance)
			if err != nil {
				return err
			}
			if err := instance.CreateSuperblock(mInfo); err != nil {
				return err
			}
		} else {
			if err := instance.CreateSuperblock(&mgmtInfo{}); err != nil {
				return err
			}
		}
	}

	return nil
}

// AwaitStorageReady blocks until all managed IOServer instances have storage
// available and ready to be used.
func (h *IOServerHarness) AwaitStorageReady(ctx context.Context, skipMissingSuperblock bool) error {
	h.RLock()
	defer h.RUnlock()

	if h.started {
		return errors.New("can't wait for storage: harness already started")
	}

	h.log.Info("Waiting for I/O server instance storage to be ready...")
	for _, instance := range h.instances {
		needsScmFormat, err := instance.NeedsScmFormat()
		if err != nil {
			h.log.Error(errors.Wrap(err, "failed to check storage formatting").Error())
			needsScmFormat = true
		}

		if !needsScmFormat {
			if skipMissingSuperblock {
				continue
			}
			h.log.Debug("no SCM format required; checking for superblock")
			needsSuperblock, err := instance.NeedsSuperblock()
			if err != nil {
				h.log.Errorf("failed to check instance superblock: %s", err)
			}
			if !needsSuperblock {
				continue
			}
		}
		h.log.Info("SCM format required")
		instance.AwaitStorageReady(ctx)
	}
	return ctx.Err()
}

// registerNewMember creates a new system.Member for given instance and adds it
// to the system membership.
func registerNewMember(membership *system.Membership, instance *IOServerInstance) error {
	m, err := instance.newMember()
	if err != nil {
		return errors.Wrap(err, "failed to get member from instance")
	}

	count, err := membership.Add(m)
	if err != nil {
		return errors.Wrap(err, "failed to add MS replica to membership")
	}

	if count != 1 {
		return errors.Errorf("expected MS replica to be first member "+
			"(want 1, got %d)", count)
	}

	return nil
}

// startInstances starts harness instances and registers system membership for
// any MS replicas (membership is normally recorded when handling join requests
// but bootstrapping MS replicas will not join).
func (h *IOServerHarness) startInstances(ctx context.Context, membership *system.Membership) error {
	h.log.Debug("starting instances")
	for _, instance := range h.instances {
		if err := instance.Start(ctx, h.errChan); err != nil {
			return err
		}

		if instance.IsMSReplica() {
			if err := registerNewMember(membership, instance); err != nil {
				return err
			}
		}
	}

	return nil
}

// waitInstancesReady awaits ready signal from I/O server before starting
// management service on MS replicas immediately so other instances can join.
// I/O server modules are then loaded.
func (h *IOServerHarness) waitInstancesReady(ctx context.Context) error {
	h.log.Debug("waiting for instances to be ready")
	for _, instance := range h.instances {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case err := <-h.errChan:
			if err != nil {
				return err
			}
		case ready := <-instance.AwaitReady():
			h.log.Debugf("instance ready: %v", ready)
			if err := instance.SetRank(ctx, ready); err != nil {
				return err
			}
		}

		if instance.IsMSReplica() {
			if err := instance.StartManagementService(); err != nil {
				return errors.Wrap(err, "failed to start management service")
			}
		}

		if err := instance.LoadModules(); err != nil {
			return errors.Wrap(err, "failed to load I/O server modules")
		}
	}

	return nil
}

// monitorInstances listens for exit results from instances or harness and will
// return only when all harness instances are stopped and restartInstances
// signal is received.
func (h *IOServerHarness) monitorInstances(ctx context.Context) error {
	h.log.Debug("monitoring instances")
	for {
		select {
		case <-ctx.Done(): // received when harness is exiting
			h.log.Debug("harness exiting")
			return ctx.Err()
		case err := <-h.errChan: // received when instance exits
			// TODO: Restart failed instances on unexpected exit.
			allInstancesStopped := !h.HasStartedInstances()
			msg := fmt.Sprintf("instance exited: %v", err)
			if allInstancesStopped {
				msg += ", all instances stopped!"
			}
			h.log.Info(msg)
		case <-h.restartInstances: // trigger harness to restart instances
			h.log.Debug("restart instances")
			if h.HasStartedInstances() {
				return errors.New("cannot restart when instances are running")
			}
			return nil
		}
	}

	return nil
}

// Start starts all configured instances, waits for them to be ready and then
// loops monitoring instance exit, harness exit and harness restart signals.
func (h *IOServerHarness) Start(parent context.Context, membership *system.Membership) error {
	if h.IsStarted() {
		return errors.New("can't start: harness already started")
	}

	// Now we want to block any RPCs that might try to mess with storage
	// (format, firmware update, etc) before attempting to start I/O servers
	// which are using the storage.
	h.Lock()
	h.started = true
	h.Unlock()

	ctx, shutdown := context.WithCancel(parent)
	defer shutdown()

	for {
		if err := h.startInstances(ctx, membership); err != nil {
			return err
		}
		if err := h.waitInstancesReady(ctx); err != nil {
			return err
		}
		if err := h.monitorInstances(ctx); err != nil {
			return err
		}
	}

	return nil
}

// HasStartedInstances returns true if any harness instances are running.
func (h *IOServerHarness) HasStartedInstances() bool {
	h.RLock()
	defer h.RUnlock()

	for _, instance := range h.instances {
		if instance.IsStarted() {
			return true
		}
	}

	return false
}

// RestartInstances will signal the harness to restart configured instances.
func (h *IOServerHarness) RestartInstances() error {
	if !h.IsStarted() {
		return errors.New("can't start instances: harness not started")
	}
	if h.HasStartedInstances() {
		return errors.New("can't start instances: already started")
	}

	h.restartInstances <- struct{}{} // trigger harness to restart its instances

	return nil
}

// StartManagementService starts the DAOS management service on this node.
func (h *IOServerHarness) StartManagementService(ctx context.Context) error {
	h.RLock()
	defer h.RUnlock()

	for _, instance := range h.instances {
		if err := instance.StartManagementService(); err != nil {
			return err
		}
	}

	return nil
}

type mgmtInfo struct {
	isReplica       bool
	shouldBootstrap bool
}

func getMgmtInfo(srv *IOServerInstance) (*mgmtInfo, error) {
	// Determine if an I/O server needs to createMS or bootstrapMS.
	var err error
	mi := &mgmtInfo{}
	mi.isReplica, mi.shouldBootstrap, err = checkMgmtSvcReplica(
		srv.msClient.cfg.ControlAddr,
		srv.msClient.cfg.AccessPoints,
	)
	if err != nil {
		return nil, err
	}

	return mi, nil
}
