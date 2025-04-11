//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"os"
	"sync"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

// Engine defines an interface to be implemented by engine instances.
//
// NB: This interface is way too big right now; need to refactor in order
// to limit scope.
type Engine interface {
	events.Publisher

	// These are definitely wrong... They indicate that too much internal
	// information is being leaked outside of the implementation.
	newCret(string, error) *ctlpb.NvmeControllerResult
	tryDrpc(context.Context, drpc.Method) *system.MemberResult
	requestStart(context.Context)
	isAwaitingFormat() bool

	// These methods should probably be replaced by callbacks.
	NotifyDrpcReady(*srvpb.NotifyReadyReq)
	NotifyStorageReady(bool)

	// These methods should probably be refactored out into functions that
	// accept the engine instance as a parameter.
	StorageFormatSCM(context.Context, bool) *ctlpb.ScmMountResult

	// This is a more reasonable surface that will be easier to maintain and test.
	CallDrpc(context.Context, drpc.Method, proto.Message) (*drpc.Response, error)
	GetRank() (ranklist.Rank, error)
	GetTargetCount() int
	Index() uint32
	IsStarted() bool
	IsReady() bool
	LocalState() system.MemberState
	RemoveSuperblock() error
	Run(context.Context)
	SetupRank(context.Context, ranklist.Rank, uint32) error
	Stop(os.Signal) error
	OnInstanceExit(...onInstanceExitFn)
	OnReady(...onReadyFn)
	GetStorage() *storage.Provider
	SetCheckerMode(bool)
	Debugf(string, ...interface{})
	Tracef(string, ...interface{})
	GetLastHealthStats(string) *ctlpb.BioHealthResp
	SetLastHealthStats(string, *ctlpb.BioHealthResp)
}

// EngineHarness is responsible for managing Engine instances.
type EngineHarness struct {
	sync.RWMutex
	log           logging.Logger
	instances     []Engine
	started       atm.Bool
	faultDomain   *system.FaultDomain
	onDrpcFailure []onDrpcFailureFn
}

// NewEngineHarness returns an initialized *EngineHarness.
func NewEngineHarness(log logging.Logger) *EngineHarness {
	return &EngineHarness{
		log:       log,
		instances: make([]Engine, 0),
	}
}

// WithFaultDomain adds a fault domain to the EngineHarness.
func (h *EngineHarness) WithFaultDomain(fd *system.FaultDomain) *EngineHarness {
	h.faultDomain = fd
	return h
}

// isStarted indicates whether the EngineHarness is in a running state.
func (h *EngineHarness) isStarted() bool {
	return h.started.Load()
}

// Instances safely returns harness' EngineInstances.
func (h *EngineHarness) Instances() []Engine {
	h.RLock()
	defer h.RUnlock()
	return h.instances
}

// FilterInstancesByRankSet returns harness' EngineInstances that match any
// of a list of ranks derived from provided rank set string.
func (h *EngineHarness) FilterInstancesByRankSet(ranks string) ([]Engine, error) {
	h.RLock()
	defer h.RUnlock()

	rankList, err := ranklist.ParseRanks(ranks)
	if err != nil {
		return nil, err
	}
	out := make([]Engine, 0)

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

// AddInstance adds a new Engine instance to be managed.
func (h *EngineHarness) AddInstance(ei Engine) error {
	if h.isStarted() {
		return errors.New("can't add instance to already-started harness")
	}

	h.Lock()
	defer h.Unlock()
	if indexSetter, ok := ei.(interface{ setIndex(uint32) }); ok {
		indexSetter.setIndex(uint32(len(h.instances)))
	}

	h.instances = append(h.instances, ei)
	return nil
}

type onDrpcFailureFn func(ctx context.Context, err error)

// OnDrpcFailure registers callbacks to be invoked on dRPC call failure.
func (h *EngineHarness) OnDrpcFailure(fns ...onDrpcFailureFn) {
	h.Lock()
	defer h.Unlock()

	h.onDrpcFailure = append(h.onDrpcFailure, fns...)
}

// CallDrpc calls the supplied dRPC method on a managed I/O Engine instance.
func (h *EngineHarness) CallDrpc(ctx context.Context, method drpc.Method, body proto.Message) (resp *drpc.Response, err error) {
	if !h.isStarted() {
		return nil, FaultHarnessNotStarted
	}

	instances := h.Instances()
	if len(instances) == 0 {
		return nil, errors.New("no engine instances to service drpc call")
	}

	// Iterate through the managed instances, looking for the first one that is available to
	// service the request. If non-transient error is returned from CallDrpc, that error will
	// be returned immediately. If a transient error is returned, continue to the next engine.
	drpcErrs := make([]error, 0, len(instances))
	for _, i := range instances {
		resp, err = i.CallDrpc(ctx, method, body)
		if err == nil {
			break
		}

		drpcErrs = append(drpcErrs, errors.Cause(err))
		msg := fmt.Sprintf("failure on engine instance %d: %s", i.Index(), err)

		switch errors.Cause(err) {
		case errEngineNotReady, errDRPCNotReady, FaultDataPlaneNotStarted:
			h.log.Debug("drpc call transient " + msg)
			continue
		}

		h.log.Debug("drpc call hard " + msg)
		break
	}

	if err == nil {
		return // Request sent.
	}

	var e error
	hasDRPCErr := false
	for _, e = range drpcErrs {
		switch e {
		case errDRPCNotReady, FaultDataPlaneNotStarted:
			// If no engines can service request and drpc specific error has
			// been returned then pass that error to the failure handlers.
			hasDRPCErr = true
			break
		}
	}
	if !hasDRPCErr {
		return // Don't trigger handlers on failures not related to dRPC comms.
	}

	h.log.Debugf("invoking dRPC failure handlers for %s", e)
	h.RLock()
	defer h.RUnlock()
	for _, fn := range h.onDrpcFailure {
		fn(ctx, e)
	}

	return
}

type dbLeader interface {
	IsLeader() bool
	ShutdownRaft() error
	ResignLeadership(error) error
}

func newOnDrpcFailureFn(log logging.Logger, db dbLeader) onDrpcFailureFn {
	return func(_ context.Context, errIn error) {
		if !db.IsLeader() {
			return
		}

		// If we cannot service a dRPC request on this node, we should resign as leader in
		// order to force a new leader election.
		if err := db.ResignLeadership(errIn); err != nil {
			log.Errorf("failed to resign leadership after dRPC failure: %s", err)
		}
	}
}

// Start starts harness by setting up and starting dRPC before initiating
// configured instances' processing loops.
//
// Run until harness is shutdown.
func (h *EngineHarness) Start(ctx context.Context, db dbLeader, cfg *config.Server) error {
	if h.isStarted() {
		return errors.New("can't start: harness already started")
	}

	if cfg == nil {
		return errors.New("nil cfg supplied to Start()")
	}

	// Now we want to block any RPCs that might try to mess with storage
	// (format, firmware update, etc) before attempting to start I/O Engines
	// which are using the storage.
	h.started.SetTrue()
	defer h.started.SetFalse()

	for _, ei := range h.Instances() {
		ei.Run(ctx)
	}

	h.OnDrpcFailure(newOnDrpcFailureFn(h.log, db))

	<-ctx.Done()
	h.log.Debug("shutting down harness")

	return ctx.Err()
}

// readyRanks returns rank assignment of configured harness instances that are
// in a ready state. Rank assignments can be nil.
func (h *EngineHarness) readyRanks() []ranklist.Rank {
	h.RLock()
	defer h.RUnlock()

	ranks := make([]ranklist.Rank, 0)
	for idx, ei := range h.instances {
		if ei.IsReady() {
			rank, err := ei.GetRank()
			if err != nil {
				h.log.Errorf("instance %d: no rank (%s)", idx, err)
				continue
			}
			ranks = append(ranks, rank)
		}
	}

	return ranks
}
