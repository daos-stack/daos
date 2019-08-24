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
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

const (
	// index in IOServerHarness.instances
	defaultManagementInstance = 0
	shutdownTimeout           = 1 * time.Second
)

// IOServerHarness is responsible for managing IOServer instances
type IOServerHarness struct {
	log       logging.Logger
	ext       External
	instances []*IOServerInstance
	started   bool
}

// NewHarness returns an initialized *IOServerHarness
func NewIOServerHarness(ext External, log logging.Logger) *IOServerHarness {
	return &IOServerHarness{
		ext:       ext,
		log:       log,
		instances: make([]*IOServerInstance, 0, 2),
	}
}

// AddInstance adds a new IOServer instance to be managed.
func (h *IOServerHarness) AddInstance(srv *IOServerInstance) error {
	if h.started {
		return errors.New("can't add instance to already-started harness")
	}

	srvIdx := len(h.instances)
	srv.Index = srvIdx
	srv.runner.Config.Index = srvIdx

	h.instances = append(h.instances, srv)
	return nil
}

// GetManagementInstance returns a managed IO Server instance
// to be used as a management target.
func (h *IOServerHarness) GetManagementInstance() (*IOServerInstance, error) {
	if len(h.instances) == 0 {
		return nil, errors.New("harness has no managed instances")
	}

	if defaultManagementInstance > len(h.instances) {
		return nil, errors.Errorf("no instance index %d", defaultManagementInstance)
	}

	// Just pick one for now.
	return h.instances[defaultManagementInstance], nil
}

// CreateSuperblocks creates instance superblocks as needed.
func (h *IOServerHarness) CreateSuperblocks(reformat bool) error {
	toFormat := make([]*IOServerInstance, 0, len(h.instances))

	for _, instance := range h.instances {
		needsFormat, err := instance.NeedsSuperblock()
		if !needsFormat {
			continue
		}
		if err != nil && !reformat {
			return nil
		}
		toFormat = append(toFormat, instance)
	}

	if len(toFormat) == 0 {
		return nil
	}

	for _, instance := range toFormat {
		// Only the first I/O server can be an MS replica.
		if instance.Index == 0 {
			msInfo, err := getMgmtInfo(instance)
			if err != nil {
				return err
			}
			if err := instance.CreateSuperblock(msInfo); err != nil {
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
	h.log.Info("Waiting for I/O server instance storage to be ready...")
	for _, instance := range h.instances {
		needsSuperblock, err := instance.NeedsSuperblock()
		if err != nil {
			return err
		}
		if needsSuperblock {
			instance.AwaitStorageReady(ctx)
		}
	}
	return ctx.Err()
}

// Start starts all configured instances and the management
// service, then waits for them to exit.
func (h *IOServerHarness) Start(parent context.Context) error {
	if h.started {
		return errors.New("harness already started")
	}

	errChan := make(chan error, len(h.instances))
	// start 'em up
	for _, instance := range h.instances {
		if err := instance.Start(parent, errChan); err != nil {
			return err
		}
	}

	// ... wait until they say they've started
	for _, instance := range h.instances {
		select {
		case <-parent.Done():
			return parent.Err()
		case err := <-errChan:
			if err != nil {
				return err
			}
		case ready := <-instance.AwaitReady():
			if pmixless() {
				h.log.Debug("PMIx-less mode detected")
				if err := instance.SetRank(parent, ready); err != nil {
					return err
				}
			}
		}
	}

	if err := h.StartManagementService(parent); err != nil {
		return errors.Wrap(err, "failed to start management service")
	}

	h.started = true
	shutdown := func() {
		// FIXME: This doesn't do anything?
		for _, srv := range h.instances {
			ctx, cancel := context.WithTimeout(context.Background(), shutdownTimeout)
			if err := srv.Shutdown(ctx); err != nil {
				idx := srv.runner.Config.Index
				h.log.Errorf("IOServer instance %d failed to shut down: %s", idx, err)
			}
			defer cancel()
		}
	}

	// now monitor them
	for {
		select {
		case <-parent.Done():
			shutdown()
			return nil
		case err := <-errChan:
			// If we receive an error from any instance, shut them all down
			if err != nil {
				shutdown()
				return errors.Wrap(err, "Instance error")
			}
		}
	}
}

func (h *IOServerHarness) StartManagementService(ctx context.Context) error {
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
