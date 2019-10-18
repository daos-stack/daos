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
	"os"
	"strconv"
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

const (
	// index in IOServerHarness.instances
	defaultManagementInstance = 0
)

// IOServerHarness is responsible for managing IOServer instances
type IOServerHarness struct {
	sync.RWMutex
	log       logging.Logger
	instances []*IOServerInstance
	started   bool
}

// NewHarness returns an initialized *IOServerHarness
func NewIOServerHarness(log logging.Logger) *IOServerHarness {
	return &IOServerHarness{
		log:       log,
		instances: make([]*IOServerInstance, 0, 2),
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
	srvIdx := len(h.instances)
	srv.Index = uint32(srvIdx)
	srv.runner.Config.Index = uint32(srvIdx)

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
		if instance.Index == 0 {
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

// AwaitStorageReady blocks until all managed IOServer instances
// have storage available and ready to be used.
func (h *IOServerHarness) AwaitStorageReady(ctx context.Context) error {
	h.RLock()
	defer h.RUnlock()

	if h.started {
		return errors.New("can't wait for storage: harness already started")
	}

	h.log.Info("Waiting for I/O server instance storage to be ready...")
	for _, instance := range h.instances {
		needsScmFormat, err := instance.NeedsScmFormat()
		if err != nil {
			return errors.Wrap(err, "failed to check storage formatting")
		}

		if !needsScmFormat {
			h.log.Debug("no SCM format required; checking for superblock")
			needsSuperblock, err := instance.NeedsSuperblock()
			if err != nil {
				return errors.Wrap(err, "failed to check instance superblock")
			}
			if !needsSuperblock {
				continue
			}
		}
		h.log.Debug("SCM format required")
		instance.AwaitStorageReady(ctx)
	}
	return ctx.Err()
}

// Start starts all configured instances and the management
// service, then waits for them to exit.
func (h *IOServerHarness) Start(parent context.Context) error {
	if h.IsStarted() {
		return errors.New("can't start: harness already started")
	}

	// Now we want to block any RPCs that might try to mess with storage
	// (format, firmware update, etc) before attempting to start I/O servers
	// which are using the storage.
	h.Lock()
	h.started = true
	h.Unlock()

	instances := h.Instances()
	ctx, shutdown := context.WithCancel(parent)
	defer shutdown()
	errChan := make(chan error, len(instances))
	// start 'em up
	for _, instance := range instances {
		if err := instance.Start(ctx, errChan); err != nil {
			return err
		}
	}

	// ... wait until they say they've started
	for _, instance := range instances {
		select {
		case <-parent.Done():
			return parent.Err()
		case err := <-errChan:
			if err != nil {
				return err
			}
		case ready := <-instance.AwaitReady():
			if pmixless() {
				h.log.Debugf("PMIx-less mode detected (ready: %v)", ready)
				if err := instance.SetRank(ctx, ready); err != nil {
					return err
				}
			}
		}
	}

	if err := h.StartManagementService(ctx); err != nil {
		return errors.Wrap(err, "failed to start management service")
	}

	// now monitor them
	for {
		select {
		case <-parent.Done():
			return nil
		case err := <-errChan:
			// If we receive an error from any instance, shut them all down.
			// TODO: Restart failed instances rather than shutting everything
			// down.
			h.log.Errorf("instance error: %s", err)
			if err != nil {
				return errors.Wrap(err, "Instance error")
			}
		}
	}
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

// pmixless returns if we are in PMIx-less or PMIx mode.
func pmixless() bool {
	if _, ok := os.LookupEnv("PMIX_RANK"); !ok {
		return true
	}
	if _, ok := os.LookupEnv("DAOS_PMIXLESS"); ok {
		return true
	}
	return false
}

// pmixRank returns the PMIx rank. If PMIx-less or PMIX_RANK has an unexpected
// value, it returns an error.
func pmixRank() (ioserver.Rank, error) {
	s, ok := os.LookupEnv("PMIX_RANK")
	if !ok {
		return ioserver.NilRank, errors.New("not in PMIx mode")
	}
	r, err := strconv.ParseUint(s, 0, 32)
	if err != nil {
		return ioserver.NilRank, errors.Wrap(err, "PMIX_RANK="+s)
	}
	return ioserver.Rank(r), nil
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
	// A temporary workaround to create and start MS before we fully
	// migrate to PMIx-less mode.
	if !pmixless() {
		rank, err := pmixRank()
		if err != nil {
			return nil, err
		}
		if rank == 0 {
			mi.isReplica = true
			mi.shouldBootstrap = true
		}
	}

	return mi, nil
}
