//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"os"
	"sync"
	"time"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	commonpb "github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	rankReqTimeout   = 30 * time.Second
	rankStartTimeout = 2 * rankReqTimeout
)

// Engine defines an interface to be implemented by engine instances.
//
// NB: This interface is way too big right now; need to refactor in order
// to limit scope.
type Engine interface {
	// These are definitely wrong... They indicate that too much internal
	// information is being leaked outside of the implementation.
	newCret(string, error) *ctlpb.NvmeControllerResult
	tryDrpc(context.Context, drpc.Method) *system.MemberResult
	requestStart(context.Context)
	updateInUseBdevs(context.Context, map[string]*storage.NvmeController) error
	isAwaitingFormat() bool

	// These methods should probably be replaced by callbacks.
	NotifyDrpcReady(*srvpb.NotifyReadyReq)
	NotifyStorageReady()
	BioErrorNotify(*srvpb.BioErrorReq)

	// These methods should probably be refactored out into functions that
	// accept the engine instance as a parameter.
	GetBioHealth(context.Context, *ctlpb.BioHealthReq) (*ctlpb.BioHealthResp, error)
	GetScmConfig() (*storage.TierConfig, error)
	GetScmUsage() (*storage.ScmMountPoint, error)
	HasBlockDevices() bool
	SetBdevCache(storage.BdevScanResponse)
	ScanBdevTiers(bool) ([]storage.BdevTierScanResult, error)
	ListSmdDevices(context.Context, *ctlpb.SmdDevReq) (*ctlpb.SmdDevResp, error)
	StorageFormatSCM(context.Context, bool) *ctlpb.ScmMountResult
	StorageFormatNVMe() commonpb.NvmeControllerResults
	StorageWriteNvmeConfig() error

	// This is a more reasonable surface that will be easier to maintain and test.
	CallDrpc(context.Context, drpc.Method, proto.Message) (*drpc.Response, error)
	GetRank() (system.Rank, error)
	GetTargetCount() int
	Index() uint32
	IsStarted() bool
	IsReady() bool
	LocalState() system.MemberState
	RemoveSuperblock() error
	Run(context.Context, bool)
	SetupRank(context.Context, system.Rank) error
	Stop(os.Signal) error
	OnInstanceExit(...onInstanceExitFn)
	OnReady(...onReadyFn)
}

// EngineHarness is responsible for managing Engine instances.
type EngineHarness struct {
	sync.RWMutex
	log              logging.Logger
	instances        []Engine
	started          atm.Bool
	rankReqTimeout   time.Duration
	rankStartTimeout time.Duration
	faultDomain      *system.FaultDomain
}

// NewEngineHarness returns an initialized *EngineHarness.
func NewEngineHarness(log logging.Logger) *EngineHarness {
	return &EngineHarness{
		log:              log,
		instances:        make([]Engine, 0),
		rankReqTimeout:   rankReqTimeout,
		rankStartTimeout: rankStartTimeout,
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

	rankList, err := system.ParseRanks(ranks)
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
func (h *EngineHarness) AddInstance(ei *EngineInstance) error {
	if h.isStarted() {
		return errors.New("can't add instance to already-started harness")
	}

	h.Lock()
	defer h.Unlock()
	ei.setIndex(uint32(len(h.instances)))

	h.instances = append(h.instances, ei)
	return nil
}

// CallDrpc calls the supplied dRPC method on a managed I/O Engine instance.
func (h *EngineHarness) CallDrpc(ctx context.Context, method drpc.Method, body proto.Message) (resp *drpc.Response, err error) {
	if !h.isStarted() {
		return nil, FaultHarnessNotStarted
	}

	// Iterate through the managed instances, looking for
	// the first one that is available to service the request.
	// If the request fails, that error will be returned.
	for _, i := range h.Instances() {
		if !i.IsReady() {
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
func (h *EngineHarness) Start(ctx context.Context, db *system.Database, ps *events.PubSub, cfg *config.Server) error {
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

	instances := h.Instances()

	drpcSetupReq := &drpcServerSetupReq{
		log:     h.log,
		sockDir: cfg.SocketDir,
		engines: instances,
		tc:      cfg.TransportConfig,
		sysdb:   db,
		events:  ps,
	}
	// Single daos_server dRPC server to handle all engine requests
	if err := drpcServerSetup(ctx, drpcSetupReq); err != nil {
		return errors.WithMessage(err, "dRPC server setup")
	}
	defer func() {
		if err := drpcCleanup(cfg.SocketDir); err != nil {
			h.log.Errorf("error during dRPC cleanup: %s", err)
		}
	}()

	for _, ei := range instances {
		ei.Run(ctx, cfg.RecreateSuperblocks)
	}

	<-ctx.Done()
	h.log.Debug("shutting down harness")

	return ctx.Err()
}

// readyRanks returns rank assignment of configured harness instances that are
// in a ready state. Rank assignments can be nil.
func (h *EngineHarness) readyRanks() []system.Rank {
	h.RLock()
	defer h.RUnlock()

	ranks := make([]system.Rank, 0)
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
