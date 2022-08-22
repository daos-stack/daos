//
// (C) Copyright 2019-2022 Intel Corporation.
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

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

type (
	systemJoinFn     func(context.Context, *control.SystemJoinReq) (*control.SystemJoinResp, error)
	onAwaitFormatFn  func(context.Context, uint32, string) error
	onStorageReadyFn func(context.Context) error
	onReadyFn        func(context.Context) error
	onInstanceExitFn func(context.Context, uint32, system.Rank, error, uint64) error
)

// EngineInstance encapsulates control-plane specific configuration
// and functionality for managed I/O Engine instances. The distinction
// between this structure and what's in the engine package is that the
// engine package is only concerned with configuring and executing
// a single daos_engine instance. EngineInstance is intended to
// be used with EngineHarness to manage and monitor multiple instances
// per node.
type EngineInstance struct {
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
	onAwaitFormat   []onAwaitFormatFn
	onStorageReady  []onStorageReadyFn
	onReady         []onReadyFn
	onInstanceExit  []onInstanceExitFn

	sync.RWMutex
	// these must be protected by a mutex in order to
	// avoid racy access.
	_cancelCtx  context.CancelFunc
	_drpcClient drpc.DomainSocketClient
	_superblock *Superblock
	_lastErr    error // populated when harness receives signal
}

// NewEngineInstance returns an *EngineInstance initialized with
// its dependencies.
func NewEngineInstance(l logging.Logger, p *storage.Provider, jf systemJoinFn, r EngineRunner) *EngineInstance {
	return &EngineInstance{
		log:            l,
		runner:         r,
		storage:        p,
		joinSystem:     jf,
		drpcReady:      make(chan *srvpb.NotifyReadyReq),
		storageReady:   make(chan bool),
		startRequested: make(chan bool),
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

// removeSocket removes the socket file used for dRPC communication with
// harness and updates relevant ready states.
func (ei *EngineInstance) removeSocket() error {
	fMsg := fmt.Sprintf("removing instance %d socket file", ei.Index())

	dc, err := ei.getDrpcClient()
	if err != nil {
		return errors.Wrap(err, fMsg)
	}
	engineSock := dc.GetSocketPath()

	if err := checkDrpcClientSocketPath(engineSock); err != nil {
		return errors.Wrap(err, fMsg)
	}
	os.Remove(engineSock)

	ei.ready.SetFalse()

	return nil
}

func (ei *EngineInstance) determineRank(ctx context.Context, ready *srvpb.NotifyReadyReq) (system.Rank, bool, error) {
	superblock := ei.getSuperblock()
	if superblock == nil {
		return system.NilRank, false, errors.New("nil superblock while determining rank")
	}

	r := system.NilRank
	if superblock.Rank != nil {
		r = *superblock.Rank
	}

	resp, err := ei.joinSystem(ctx, &control.SystemJoinReq{
		UUID:        superblock.UUID,
		Rank:        r,
		URI:         ready.GetUri(),
		NumContexts: ready.GetNctxs(),
		FaultDomain: ei.hostFaultDomain,
		InstanceIdx: ei.Index(),
		Incarnation: ready.GetIncarnation(),
	})
	if err != nil {
		ei.log.Errorf("join failed: %s", err)
		return system.NilRank, false, err
	} else if resp.State == system.MemberStateExcluded {
		return system.NilRank, resp.LocalJoin, errors.Errorf("rank %d excluded", resp.Rank)
	}
	r = system.Rank(resp.Rank)

	// TODO: Check to see if ready.Uri != superblock.URI, which might
	// need to trigger some kind of update?

	if !superblock.ValidRank {
		superblock.Rank = new(system.Rank)
		*superblock.Rank = r
		superblock.ValidRank = true
		superblock.URI = ready.GetUri()
		ei.setSuperblock(superblock)
		if err := ei.WriteSuperblock(); err != nil {
			return system.NilRank, resp.LocalJoin, err
		}
	}

	return r, resp.LocalJoin, nil
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

	r, localJoin, err := ei.determineRank(ctx, ready)
	if err != nil {
		return err
	}

	// If the join was already processed because it ran on the same server,
	// skip the rest of these steps.
	if localJoin {
		return nil
	}

	return ei.SetupRank(ctx, r)
}

func (ei *EngineInstance) SetupRank(ctx context.Context, rank system.Rank) error {
	if err := ei.callSetRank(ctx, rank); err != nil {
		return errors.Wrap(err, "SetRank failed")
	}

	if err := ei.callSetUp(ctx); err != nil {
		return errors.Wrap(err, "SetUp failed")
	}

	ei.ready.SetTrue()
	return nil
}

func (ei *EngineInstance) callSetRank(ctx context.Context, rank system.Rank) error {
	dresp, err := ei.CallDrpc(ctx, drpc.MethodSetRank, &mgmtpb.SetRankReq{Rank: rank.Uint32()})
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
func (ei *EngineInstance) GetRank() (system.Rank, error) {
	var err error
	sb := ei.getSuperblock()

	switch {
	case sb == nil:
		err = errors.New("nil superblock")
	case sb.Rank == nil:
		err = errors.New("nil rank in superblock")
	}

	if err != nil {
		return system.NilRank, err
	}

	return *sb.Rank, nil
}

// setMemSize updates memory size in engine config.
func (ei *EngineInstance) setMemSize(memSizeMb int) {
	ei.Lock()
	defer ei.Unlock()

	ei.runner.GetConfig().MemSize = memSizeMb
}

// setHugePageSz updates hugepage size in engine config.
func (ei *EngineInstance) setHugePageSz(hpSizeMb int) {
	ei.Lock()
	defer ei.Unlock()

	ei.runner.GetConfig().HugePageSz = hpSizeMb
}

// setTargetCount updates target count in engine config.
func (ei *EngineInstance) setTargetCount(numTargets int) {
	ei.Lock()
	defer ei.Unlock()

	ei.runner.GetConfig().TargetCount = numTargets
}

// GetTargetCount returns the target count set for this instance.
func (ei *EngineInstance) GetTargetCount() int {
	ei.RLock()
	defer ei.RUnlock()

	return ei.runner.GetConfig().TargetCount
}

func (ei *EngineInstance) callSetUp(ctx context.Context) error {
	dresp, err := ei.CallDrpc(ctx, drpc.MethodSetUp, nil)
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

// BioErrorNotify logs a blob I/O error.
func (ei *EngineInstance) BioErrorNotify(bio *srvpb.BioErrorReq) {

	ei.log.Errorf("I/O Engine instance %d (target %d) has detected blob I/O error! %v",
		ei.Index(), bio.TgtId, bio)
}
