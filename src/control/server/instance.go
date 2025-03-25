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
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

type (
	systemJoinFn     func(context.Context, *control.SystemJoinReq) (*control.SystemJoinResp, error)
	onAwaitFormatFn  func(context.Context, uint32, string) error
	onStorageReadyFn func(context.Context) error
	onReadyFn        func(context.Context) error
	onInstanceExitFn func(context.Context, uint32, ranklist.Rank, error, int) error
)

// EngineInstance encapsulates control-plane specific configuration
// and functionality for managed I/O Engine instances. The distinction
// between this structure and what's in the engine package is that the
// engine package is only concerned with configuring and executing
// a single daos_engine instance. EngineInstance is intended to
// be used with EngineHarness to manage and monitor multiple instances
// per node.
type EngineInstance struct {
	events.Publisher

	log             logging.Logger
	runner          EngineRunner
	storage         *storage.Provider
	waitFormat      atm.Bool
	storageReady    chan bool
	waitDrpc        atm.Bool
	drpcReady       chan *srvpb.NotifyReadyReq
	ready           atm.Bool
	startRequested  chan bool
	fsRoot          string
	hostFaultDomain *system.FaultDomain
	joinSystem      systemJoinFn
	replaceRank     atm.Bool
	onAwaitFormat   []onAwaitFormatFn
	onStorageReady  []onStorageReadyFn
	onReady         []onReadyFn
	onInstanceExit  []onInstanceExitFn
	getDrpcClientFn func(string) drpc.DomainSocketClient

	sync.RWMutex
	// these must be protected by a mutex in order to
	// avoid racy access.
	_drpcSocket      string
	_cancelCtx       context.CancelFunc
	_superblock      *Superblock
	_lastErr         error // populated when harness receives signal
	_lastHealthStats map[string]*ctlpb.BioHealthResp
}

// NewEngineInstance returns an *EngineInstance initialized with its dependencies.
func NewEngineInstance(l logging.Logger, p *storage.Provider, jf systemJoinFn, r EngineRunner, ps *events.PubSub) *EngineInstance {
	return &EngineInstance{
		log:              l,
		runner:           r,
		storage:          p,
		joinSystem:       jf,
		drpcReady:        make(chan *srvpb.NotifyReadyReq),
		storageReady:     make(chan bool),
		startRequested:   make(chan bool),
		Publisher:        ps,
		_lastHealthStats: make(map[string]*ctlpb.BioHealthResp),
	}
}

// WithHostFaultDomain adds a fault domain for the host this instance is running
// on.
func (ei *EngineInstance) WithHostFaultDomain(fd *system.FaultDomain) *EngineInstance {
	ei.hostFaultDomain = fd
	return ei
}

// isAwaitingFormat indicates whether EngineInstance is waiting
// for an administrator action to trigger a format.
func (ei *EngineInstance) isAwaitingFormat() bool {
	return ei.waitFormat.Load()
}

// IsStarted indicates whether EngineInstance is in a running state.
func (ei *EngineInstance) IsStarted() bool {
	return ei.runner.IsRunning()
}

// IsReady indicates whether the EngineInstance is in a ready state.
//
// If true indicates that the instance is fully setup, distinct from
// drpc and storage ready states, and currently active.
func (ei *EngineInstance) IsReady() bool {
	return ei.ready.Load() && ei.IsStarted()
}

// OnAwaitFormat adds a list of callbacks to invoke when the instance
// requires formatting.
func (ei *EngineInstance) OnAwaitFormat(fns ...onAwaitFormatFn) {
	ei.onAwaitFormat = append(ei.onAwaitFormat, fns...)
}

// OnStorageReady adds a list of callbacks to invoke when the instance
// storage becomes ready.
func (ei *EngineInstance) OnStorageReady(fns ...onStorageReadyFn) {
	ei.onStorageReady = append(ei.onStorageReady, fns...)
}

// OnReady adds a list of callbacks to invoke when the instance
// becomes ready.
func (ei *EngineInstance) OnReady(fns ...onReadyFn) {
	ei.onReady = append(ei.onReady, fns...)
}

// OnInstanceExit adds a list of callbacks to invoke when the instance
// runner (process) terminates.
func (ei *EngineInstance) OnInstanceExit(fns ...onInstanceExitFn) {
	ei.onInstanceExit = append(ei.onInstanceExit, fns...)
}

// LocalState returns local perspective of the current instance state
// (doesn't consider state info held by the global system membership).
func (ei *EngineInstance) LocalState() system.MemberState {
	switch {
	case ei.IsReady():
		return system.MemberStateReady
	case ei.IsStarted():
		return system.MemberStateStarting
	case ei.isAwaitingFormat():
		return system.MemberStateAwaitFormat
	default:
		return system.MemberStateStopped
	}
}

// setIndex sets the server index assigned by the harness.
func (ei *EngineInstance) setIndex(idx uint32) {
	ei.runner.GetConfig().Index = idx
}

// Index returns the server index assigned by the harness.
func (ei *EngineInstance) Index() uint32 {
	return ei.runner.GetConfig().Index
}

// SetCheckerMode adjusts the engine configuration to enable or disable
// starting the engine in checker mode.
func (ei *EngineInstance) SetCheckerMode(enabled bool) {
	ei.runner.GetConfig().CheckerEnabled = enabled
}

// removeSocket removes the socket file used for dRPC communication with
// harness and updates relevant ready states.
func (ei *EngineInstance) removeSocket() error {
	fMsg := fmt.Sprintf("removing instance %d socket file", ei.Index())

	engineSock := ei.getDrpcSocket()

	if err := checkDrpcClientSocketPath(engineSock); err != nil {
		return errors.Wrap(err, fMsg)
	}
	os.Remove(engineSock)

	ei.ready.SetFalse()

	return nil
}

func (ei *EngineInstance) determineRank(ctx context.Context, ready *srvpb.NotifyReadyReq) (ranklist.Rank, bool, uint32, error) {
	superblock := ei.getSuperblock()
	if superblock == nil {
		return ranklist.NilRank, false, 0, errors.New("nil superblock while determining rank")
	}

	r := ranklist.NilRank
	if superblock.Rank != nil {
		r = *superblock.Rank
	}

	joinReq := &control.SystemJoinReq{
		UUID:                 superblock.UUID,
		Rank:                 r,
		URI:                  ready.GetUri(),
		SecondaryURIs:        ready.GetSecondaryUris(),
		NumContexts:          ready.GetNctxs(),
		NumSecondaryContexts: ready.GetSecondaryNctxs(),
		FaultDomain:          ei.hostFaultDomain,
		InstanceIdx:          ei.Index(),
		Incarnation:          ready.GetIncarnation(),
		CheckMode:            ready.GetCheckMode(),
		Replace:              ei.replaceRank.Load(),
	}

	resp, err := ei.joinSystem(ctx, joinReq)
	if err != nil {
		ei.log.Errorf("join failed: %s", err)
		return ranklist.NilRank, false, 0, err
	}
	switch resp.State {
	case system.MemberStateAdminExcluded, system.MemberStateExcluded:
		return ranklist.NilRank, resp.LocalJoin, 0, errors.Errorf("rank %d excluded", resp.Rank)
	case system.MemberStateCheckerStarted:
		// If the system is in checker mode but the rank was not started in
		// checker mode, we need to restart it in order to get the correct
		// modules loaded.
		if !ready.GetCheckMode() {
			ei.log.Noticef("restarting rank %d in checker mode", resp.Rank)
			go ei.requestStart(context.Background())
			ei.SetCheckerMode(true)
			return ranklist.NilRank, resp.LocalJoin, 0, errors.Errorf("rank %d restarting to enable checker", resp.Rank)
		}
	}
	r = ranklist.Rank(resp.Rank)

	// Reset replaceRank state for instance after joinSystem() has returned.
	ei.replaceRank.SetFalse()

	if !superblock.ValidRank || ready.Uri != superblock.URI {
		ei.log.Noticef("updating rank %d URI to %s", resp.Rank, ready.Uri)
		superblock.Rank = new(ranklist.Rank)
		*superblock.Rank = r
		superblock.ValidRank = true
		superblock.URI = ready.Uri
		ei.setSuperblock(superblock)
		if err := ei.WriteSuperblock(); err != nil {
			return ranklist.NilRank, resp.LocalJoin, 0, err
		}
	}

	return r, resp.LocalJoin, resp.MapVersion, nil
}

func (ei *EngineInstance) updateFaultDomainInSuperblock() error {
	if ei.hostFaultDomain == nil {
		return errors.New("engine instance has a nil fault domain")
	}

	superblock := ei.getSuperblock()
	if superblock == nil {
		return errors.New("nil superblock while updating fault domain")
	}

	newDomainStr := ei.hostFaultDomain.String()
	if newDomainStr == superblock.HostFaultDomain {
		// No change
		return nil
	}

	ei.log.Infof("instance %d setting host fault domain to %q (previously %q)",
		ei.Index(), ei.hostFaultDomain, superblock.HostFaultDomain)
	superblock.HostFaultDomain = newDomainStr

	ei.setSuperblock(superblock)
	if err := ei.WriteSuperblock(); err != nil {
		return err
	}
	return nil
}

// handleReady determines the instance rank and sends a SetRank dRPC request
// to the Engine.
func (ei *EngineInstance) handleReady(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	if err := ei.updateFaultDomainInSuperblock(); err != nil {
		ei.log.Error(err.Error()) // nonfatal
	}

	r, localJoin, mapVersion, err := ei.determineRank(ctx, ready)
	if err != nil {
		return err
	}

	// If the join was already processed because it ran on the same server,
	// skip the rest of these steps.
	if localJoin {
		return nil
	}

	return ei.SetupRank(ctx, r, mapVersion)
}

func (ei *EngineInstance) SetupRank(ctx context.Context, rank ranklist.Rank, map_version uint32) error {
	if ei.IsReady() {
		ei.log.Debugf("SetupRank called on an already set-up instance %d", ei.Index())
		return nil
	}

	if err := ei.callSetRank(ctx, rank, map_version); err != nil {
		return errors.Wrap(err, "SetRank failed")
	}

	if err := ei.callSetUp(ctx); err != nil {
		return errors.Wrap(err, "SetUp failed")
	}

	ei.ready.SetTrue()
	return nil
}

func (ei *EngineInstance) callSetRank(ctx context.Context, rank ranklist.Rank, map_version uint32) error {
	dresp, err := ei.callDrpc(ctx, drpc.MethodSetRank, &mgmtpb.SetRankReq{Rank: rank.Uint32(), MapVersion: map_version})
	if err != nil {
		return err
	}

	resp := &mgmtpb.DaosResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal SetRank response")
	}
	if resp.Status != 0 {
		return errors.Errorf("SetRank: %d\n", resp.Status)
	}

	return nil
}

// GetRank returns a valid instance rank or error.
func (ei *EngineInstance) GetRank() (ranklist.Rank, error) {
	var err error
	sb := ei.getSuperblock()

	switch {
	case sb == nil:
		err = errors.New("nil superblock")
	case sb.Rank == nil:
		err = errors.New("nil rank in superblock")
	}

	if err != nil {
		return ranklist.NilRank, err
	}

	return *sb.Rank, nil
}

// setMemSize updates memory size in engine config.
func (ei *EngineInstance) setMemSize(memSizeMb int) {
	ei.Lock()
	defer ei.Unlock()

	ei.runner.GetConfig().MemSize = memSizeMb
}

// setHugepageSz updates hugepage size in engine config.
func (ei *EngineInstance) setHugepageSz(hpSizeMb int) {
	ei.Lock()
	defer ei.Unlock()

	ei.runner.GetConfig().HugepageSz = hpSizeMb
}

// GetTargetCount returns the target count set for this instance.
func (ei *EngineInstance) GetTargetCount() int {
	ei.RLock()
	defer ei.RUnlock()

	return ei.runner.GetConfig().TargetCount
}

func (ei *EngineInstance) callSetUp(ctx context.Context) error {
	dresp, err := ei.callDrpc(ctx, drpc.MethodSetUp, nil)
	if err != nil {
		return err
	}

	resp := &mgmtpb.DaosResp{}
	if err := proto.Unmarshal(dresp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal SetUp response")
	}
	if resp.Status != 0 {
		return errors.Errorf("SetUp: %d\n", resp.Status)
	}

	return nil
}

func (ei *EngineInstance) Debugf(format string, args ...interface{}) {
	ei.log.Debugf(format, args...)
}

func (ei *EngineInstance) Tracef(format string, args ...interface{}) {
	ei.log.Tracef(format, args...)
}

func (ei *EngineInstance) GetLastHealthStats(pciAddr string) *ctlpb.BioHealthResp {
	ei.RLock()
	defer ei.RUnlock()

	if ei._lastHealthStats == nil {
		ei._lastHealthStats = make(map[string]*ctlpb.BioHealthResp)
	}
	return ei._lastHealthStats[pciAddr]
}

func (ei *EngineInstance) SetLastHealthStats(pciAddr string, bhr *ctlpb.BioHealthResp) {
	ei.Lock()
	defer ei.Unlock()

	if ei._lastHealthStats == nil {
		ei._lastHealthStats = make(map[string]*ctlpb.BioHealthResp)
	}
	ei._lastHealthStats[pciAddr] = bhr
}
