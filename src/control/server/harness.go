//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"sync"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	defaultRequestTimeout = 3 * time.Second
	defaultStartTimeout   = 10 * defaultRequestTimeout
)

// IOServerHarness is responsible for managing IOServer instances.
type IOServerHarness struct {
	sync.RWMutex
	log              logging.Logger
	instances        []*IOServerInstance
	started          atm.Bool
	rankReqTimeout   time.Duration
	rankStartTimeout time.Duration
	faultDomain      *system.FaultDomain
}

// NewIOServerHarness returns an initialized *IOServerHarness.
func NewIOServerHarness(log logging.Logger) *IOServerHarness {
	return &IOServerHarness{
		log:              log,
		instances:        make([]*IOServerInstance, 0),
		started:          atm.NewBool(false),
		rankReqTimeout:   defaultRequestTimeout,
		rankStartTimeout: defaultStartTimeout,
	}
}

// WithFaultDomain adds a fault domain to the IOServerHarness.
func (h *IOServerHarness) WithFaultDomain(fd *system.FaultDomain) *IOServerHarness {
	h.faultDomain = fd
	return h
}

// isStarted indicates whether the IOServerHarness is in a running state.
func (h *IOServerHarness) isStarted() bool {
	return h.started.Load()
}

// Instances safely returns harness' IOServerInstances.
func (h *IOServerHarness) Instances() []*IOServerInstance {
	h.RLock()
	defer h.RUnlock()
	return h.instances
}

// FilterInstancesByRankSet returns harness' IOServerInstances that match any
// of a list of ranks derived from provided rank set string.
func (h *IOServerHarness) FilterInstancesByRankSet(ranks string) ([]*IOServerInstance, error) {
	h.RLock()
	defer h.RUnlock()

	rankList, err := system.ParseRanks(ranks)
	if err != nil {
		return nil, err
	}
	out := make([]*IOServerInstance, 0)

	for _, i := range h.instances {
		r, err := i.GetRank()
		if err != nil {
			continue // no rank to check against
		}
		if r.InList(rankList) {
			out = append(out, i)
		}
	}

	return out, nil
}

// AddInstance adds a new IOServer instance to be managed.
func (h *IOServerHarness) AddInstance(srv *IOServerInstance) error {
	if h.isStarted() {
		return errors.New("can't add instance to already-started harness")
	}

	h.Lock()
	defer h.Unlock()
	srv.setIndex(uint32(len(h.instances)))

	h.instances = append(h.instances, srv)
	return nil
}

// CallDrpc calls the supplied dRPC method on a managed I/O server instance.
func (h *IOServerHarness) CallDrpc(ctx context.Context, method drpc.Method, body proto.Message) (resp *drpc.Response, err error) {
	if !h.isStarted() {
		return nil, FaultHarnessNotStarted
	}

	// Iterate through the managed instances, looking for
	// the first one that is available to service the request.
	// If the request fails, that error will be returned.
	for _, i := range h.Instances() {
		if !i.isReady() {
			err = errInstanceNotReady
			continue
		}
		resp, err = i.CallDrpc(ctx, method, body)

		switch errors.Cause(err) {
		case errDRPCNotReady, FaultDataPlaneNotStarted:
			continue
		default:
			return
		}
	}

	return
}

// Start starts harness by setting up and starting dRPC before initiating
// configured instances' processing loops.
//
// Run until harness is shutdown.
func (h *IOServerHarness) Start(ctx context.Context, db *system.Database, ps *events.PubSub, cfg *config.Server) error {
	if h.isStarted() {
		return errors.New("can't start: harness already started")
	}

	// Now we want to block any RPCs that might try to mess with storage
	// (format, firmware update, etc) before attempting to start I/O servers
	// which are using the storage.
	h.started.SetTrue()
	defer h.started.SetFalse()

	instances := h.Instances()

	if cfg != nil {
		drpcSetupReq := &drpcServerSetupReq{
			log:     h.log,
			sockDir: cfg.SocketDir,
			iosrvs:  instances,
			tc:      cfg.TransportConfig,
			sysdb:   db,
			events:  ps,
		}
		// Single daos_server dRPC server to handle all iosrv requests
		if err := drpcServerSetup(ctx, drpcSetupReq); err != nil {
			return errors.WithMessage(err, "dRPC server setup")
		}
		defer func() {
			if err := drpcCleanup(cfg.SocketDir); err != nil {
				h.log.Errorf("error during dRPC cleanup: %s", err)
			}
		}()
	}

	for _, srv := range instances {
		// start first time then relinquish control to instance
		go srv.Run(ctx, cfg.RecreateSuperblocks)
		srv.startLoop <- true
	}

	<-ctx.Done()
	h.log.Debug("shutting down harness")

	return ctx.Err()
}

// readyRanks returns rank assignment of configured harness instances that are
// in a ready state. Rank assignments can be nil.
func (h *IOServerHarness) readyRanks() []system.Rank {
	h.RLock()
	defer h.RUnlock()

	ranks := make([]system.Rank, 0)
	for _, srv := range h.instances {
		if srv.hasSuperblock() && srv.isReady() {
			ranks = append(ranks, *srv.getSuperblock().Rank)
		}
	}

	return ranks
}
