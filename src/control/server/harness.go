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
	"sync/atomic"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

// IOServerHarness is responsible for managing IOServer instances
type IOServerHarness struct {
	sync.RWMutex
	log         logging.Logger
	instances   []*IOServerInstance
	started     uint32
	restartable uint32
	restart     chan struct{}
	errChan     chan error
}

// NewHarness returns an initialized *IOServerHarness
func NewIOServerHarness(log logging.Logger) *IOServerHarness {
	return &IOServerHarness{
		log:       log,
		instances: make([]*IOServerInstance, 0, maxIoServers),
		restart:   make(chan struct{}, 1),
		errChan:   make(chan error, maxIoServers),
	}
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
	for _, mi := range h.instances {
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

	if h.IsStarted() {
		return errors.New("can't wait for storage: harness already started")
	}

	h.log.Infof("Waiting for %s instance storage to be ready...", DataPlaneName)
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
		h.log.Infof("SCM format required on instance %d", instance.Index())
		instance.AwaitStorageReady(ctx)
	}
	return ctx.Err()
}

// registerNewMember creates a new system.Member for given instance and adds it
// to the system membership.
func (h *IOServerHarness) registerNewMember(membership *system.Membership, instance *IOServerInstance) error {
	m, err := instance.newMember()
	if err != nil {
		return errors.Wrap(err, "failed to get member from instance")
	}

	created, oldState := membership.AddOrUpdate(m)
	if created {
		h.log.Debugf("bootstrapping system member: rank %d, addr %s",
			m.Rank, m.Addr)
	} else {
		h.log.Debugf("updated bootstrapping system member: rank %d, addr %s, %s->%s",
			m.Rank, m.Addr, *oldState, m.State())
		if *oldState == m.State() {
			h.log.Errorf("unexpected same state in rank %d update (%s->%s)",
				m.Rank, *oldState, m.State())
		}
	}

	return nil
}

// startInstances starts harness instances and registers system membership for
// any MS replicas (membership is normally recorded when handling join requests
// but bootstrapping MS replicas will not join).
func (h *IOServerHarness) startInstances(ctx context.Context, membership *system.Membership) error {
	h.log.Debug("starting instances")
	for _, instance := range h.Instances() {
		if err := instance.Start(ctx, h.errChan); err != nil {
			return err
		}

		if instance.IsMSReplica() {
			if err := h.registerNewMember(membership, instance); err != nil {
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
	for _, instance := range h.Instances() {
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

// monitor listens for exit results from instances or harness and will
// return only when all harness instances are stopped and restart
// signal is received.
func (h *IOServerHarness) monitor(ctx context.Context) error {
	h.log.Debug("monitoring instances")
	for {
		select {
		case <-ctx.Done(): // received when harness is exiting
			return ctx.Err()
		case err := <-h.errChan: // received when instance exits
			// TODO: Restart failed instances on unexpected exit.
			msg := fmt.Sprintf("instance exited: %v", err)
			if len(h.StartedRanks()) == 0 {
				msg += ", all instances stopped!"
				h.setRestartable()
			}
			h.log.Info(msg)
		case <-h.restart: // trigger harness to restart instances
			return nil
		}
	}

	return nil
}

// Start starts all configured instances, waits for them to be ready and then
// loops monitoring instance exit, harness exit and harness restart signals.
func (h *IOServerHarness) Start(parent context.Context, membership *system.Membership, cfg *Configuration) error {
	if h.IsStarted() {
		return errors.New("can't start: harness already started")
	}

	// Now we want to block any RPCs that might try to mess with storage
	// (format, firmware update, etc) before attempting to start I/O servers
	// which are using the storage.
	h.setStarted()
	defer h.setStopped()

	ctx, shutdown := context.WithCancel(parent)
	defer shutdown()

	for {
		if cfg != nil {
			// Single daos_server dRPC server to handle all iosrv requests
			if err := drpcSetup(ctx, h.log, cfg.SocketDir, h.Instances(), cfg.TransportConfig); err != nil {
				return errors.WithMessage(err, "dRPC setup")
			}
		}
		if err := h.startInstances(ctx, membership); err != nil {
			return err
		}
		if err := h.waitInstancesReady(ctx); err != nil {
			return err
		}
		if err := h.monitor(ctx); err != nil {
			return err
		}
	}

	return nil
}

// RestartInstances will signal the harness to restart configured instances.
func (h *IOServerHarness) RestartInstances() error {
	h.RLock()
	defer h.RUnlock()

	if !h.IsStarted() {
		return errors.New("can't start instances: harness not started")
	}
	if !h.IsRestartable() {
		return errors.New("can't start instances: already running")
	}
	if len(h.StartedRanks()) > 0 {
		return errors.New("can't start instances: already started")
	}

	h.restart <- struct{}{} // trigger harness to restart its instances

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

func (h *IOServerHarness) setStarted() {
	atomic.StoreUint32(&h.started, 1)
}

func (h *IOServerHarness) setStopped() {
	atomic.StoreUint32(&h.started, 0)
}

func (h *IOServerHarness) IsStarted() bool {
	return atomic.LoadUint32(&h.started) == 1
}

// StartedRanks returns rank assignment of configured harness instances that are
// in a running state. Rank assignments can be nil.
func (h *IOServerHarness) StartedRanks() []*ioserver.Rank {
	h.RLock()
	defer h.RUnlock()

	ranks := make([]*ioserver.Rank, 0, maxIoServers)
	for _, i := range h.instances {
		if i.hasSuperblock() && i.IsStarted() {
			ranks = append(ranks, i.getSuperblock().Rank)
		}
	}

	return ranks
}

func (h *IOServerHarness) setRestartable() {
	atomic.StoreUint32(&h.restartable, 1)
}

func (h *IOServerHarness) setNotRestartable() {
	atomic.StoreUint32(&h.restartable, 0)
}

func (h *IOServerHarness) IsRestartable() bool {
	return atomic.LoadUint32(&h.restartable) == 1
}
