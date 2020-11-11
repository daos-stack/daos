//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"os"
	"sync"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

type onStorageReadyFn func(ctx context.Context) error
type onReadyFn func(ctx context.Context) error

// IOServerInstance encapsulates control-plane specific configuration
// and functionality for managed I/O server instances. The distinction
// between this structure and what's in the ioserver package is that the
// ioserver package is only concerned with configuring and executing
// a single daos_io_server instance. IOServerInstance is intended to
// be used with IOServerHarness to manage and monitor multiple instances
// per node.
type IOServerInstance struct {
	log               logging.Logger
	runner            IOServerRunner
	bdevClassProvider *bdev.ClassProvider
	scmProvider       *scm.Provider
	msClient          *mgmtSvcClient
	waitFormat        atm.Bool
	storageReady      chan bool
	waitDrpc          atm.Bool
	drpcReady         chan *srvpb.NotifyReadyReq
	ready             atm.Bool
	startLoop         chan bool // restart loop
	fsRoot            string
	hostFaultDomain   *system.FaultDomain
	onStorageReady    []onStorageReadyFn
	onReady           []onReadyFn

	sync.RWMutex
	// these must be protected by a mutex in order to
	// avoid racy access.
	_drpcClient drpc.DomainSocketClient
	_superblock *Superblock
	_lastErr    error // populated when harness receives signal
}

// NewIOServerInstance returns an *IOServerInstance initialized with
// its dependencies.
func NewIOServerInstance(log logging.Logger,
	bcp *bdev.ClassProvider, sp *scm.Provider,
	msc *mgmtSvcClient, r IOServerRunner) *IOServerInstance {

	return &IOServerInstance{
		log:               log,
		runner:            r,
		bdevClassProvider: bcp,
		scmProvider:       sp,
		msClient:          msc,
		drpcReady:         make(chan *srvpb.NotifyReadyReq),
		storageReady:      make(chan bool),
		startLoop:         make(chan bool),
	}
}

// WithHostFaultDomain adds a fault domain for the host this instance is running
// on.
func (srv *IOServerInstance) WithHostFaultDomain(fd *system.FaultDomain) *IOServerInstance {
	srv.hostFaultDomain = fd
	return srv
}

// isAwaitingFormat indicates whether IOServerInstance is waiting
// for an administrator action to trigger a format.
func (srv *IOServerInstance) isAwaitingFormat() bool {
	return srv.waitFormat.Load()
}

// isStarted indicates whether IOServerInstance is in a running state.
func (srv *IOServerInstance) isStarted() bool {
	return srv.runner.IsRunning()
}

// isReady indicates whether the IOServerInstance is in a ready state.
//
// If true indicates that the instance is fully setup, distinct from
// drpc and storage ready states, and currently active.
func (srv *IOServerInstance) isReady() bool {
	return srv.ready.Load() && srv.isStarted()
}

// OnStorageReady adds a list of callbacks to invoke when the instance
// storage becomes ready.
func (srv *IOServerInstance) OnStorageReady(fns ...onStorageReadyFn) {
	srv.onStorageReady = append(srv.onStorageReady, fns...)
}

// OnReady adds a list of callbacks to invoke when the instance
// becomes ready.
func (srv *IOServerInstance) OnReady(fns ...onReadyFn) {
	srv.onReady = append(srv.onReady, fns...)
}

// LocalState returns local perspective of the current instance state
// (doesn't consider state info held by the global system membership).
func (srv *IOServerInstance) LocalState() system.MemberState {
	switch {
	case srv.isReady():
		return system.MemberStateReady
	case srv.isStarted():
		return system.MemberStateStarting
	case srv.isAwaitingFormat():
		return system.MemberStateAwaitFormat
	default:
		return system.MemberStateStopped
	}
}

// setIndex sets the server index assigned by the harness.
func (srv *IOServerInstance) setIndex(idx uint32) {
	srv.runner.GetConfig().Index = idx
}

// Index returns the server index assigned by the harness.
func (srv *IOServerInstance) Index() uint32 {
	return srv.runner.GetConfig().Index
}

// removeSocket removes the socket file used for dRPC communication with
// harness and updates relevant ready states.
func (srv *IOServerInstance) removeSocket() error {
	fMsg := fmt.Sprintf("removing instance %d socket file", srv.Index())

	dc, err := srv.getDrpcClient()
	if err != nil {
		return errors.Wrap(err, fMsg)
	}
	srvSock := dc.GetSocketPath()

	if err := checkDrpcClientSocketPath(srvSock); err != nil {
		return errors.Wrap(err, fMsg)
	}
	os.Remove(srvSock)

	srv.ready.SetFalse()

	return nil
}

func (srv *IOServerInstance) determineRank(ctx context.Context, ready *srvpb.NotifyReadyReq) (system.Rank, bool, error) {
	superblock := srv.getSuperblock()
	if superblock == nil {
		return system.NilRank, false, errors.New("nil superblock while determining rank")
	}

	r := system.NilRank
	if superblock.Rank != nil {
		r = *superblock.Rank
	}

	if !superblock.ValidRank {
		// FIXME DAOS-5656: retain dependency on rank 0
		if superblock.BootstrapMS {
			r = system.Rank(0)
			srv.log.Debugf("marking instance %d as rank 0", srv.Index())
		}
	}

	resp, err := srv.msClient.Join(ctx, &mgmtpb.JoinReq{
		Uuid:           superblock.UUID,
		Rank:           r.Uint32(),
		Uri:            ready.Uri,
		Nctxs:          ready.Nctxs,
		SrvFaultDomain: srv.hostFaultDomain.String(),
		Idx:            srv.Index(),
		// Addr member populated in msClient
	})
	if err != nil {
		return system.NilRank, resp.LocalJoin, err
	} else if resp.State == mgmtpb.JoinResp_OUT {
		return system.NilRank, resp.LocalJoin, errors.Errorf("rank %d excluded", resp.Rank)
	}
	r = system.Rank(resp.Rank)

	if !superblock.ValidRank {
		superblock.Rank = new(system.Rank)
		*superblock.Rank = r
		superblock.ValidRank = true
		srv.setSuperblock(superblock)
		if err := srv.WriteSuperblock(); err != nil {
			return system.NilRank, resp.LocalJoin, err
		}
	}

	return r, resp.LocalJoin, nil
}

// joinSystem determines the instance rank and sends a SetRank dRPC request
// to the IOServer.
func (srv *IOServerInstance) joinSystem(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	r, localJoin, err := srv.determineRank(ctx, ready)
	if err != nil {
		return err
	}

	// If the join was already processed because it ran on the same server,
	// skip the rest of these steps.
	if localJoin {
		return nil
	}

	if err := srv.callSetRank(ctx, r); err != nil {
		return err
	}

	if err := srv.callSetUp(ctx); err != nil {
		return err
	}

	return nil
}

func (srv *IOServerInstance) callSetRank(ctx context.Context, rank system.Rank) error {
	dresp, err := srv.CallDrpc(ctx, drpc.MethodSetRank, &mgmtpb.SetRankReq{Rank: rank.Uint32()})
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
func (srv *IOServerInstance) GetRank() (system.Rank, error) {
	var err error
	sb := srv.getSuperblock()

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

// setTargetCount updates target count in ioserver config.
func (srv *IOServerInstance) setTargetCount(numTargets int) {
	srv.Lock()
	defer srv.Unlock()

	srv.runner.GetConfig().TargetCount = numTargets
}

// GetTargetCount returns the target count set for this instance.
func (srv *IOServerInstance) GetTargetCount() int {
	srv.RLock()
	defer srv.RUnlock()

	return srv.runner.GetConfig().TargetCount
}

func (srv *IOServerInstance) callSetUp(ctx context.Context) error {
	dresp, err := srv.CallDrpc(ctx, drpc.MethodSetUp, nil)
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
func (srv *IOServerInstance) BioErrorNotify(bio *srvpb.BioErrorReq) {

	srv.log.Errorf("I/O server instance %d (target %d) has detected blob I/O error! %v",
		srv.Index(), bio.TgtId, bio)
}
