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
	"net"
	"os"
	"sync"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
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

// isMSReplica indicates whether or not this instance is a management service replica.
func (srv *IOServerInstance) isMSReplica() bool {
	return srv.hasSuperblock() && srv.getSuperblock().MS
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

func (srv *IOServerInstance) determineRank(ctx context.Context, ready *srvpb.NotifyReadyReq) (system.Rank, error) {
	superblock := srv.getSuperblock()
	if superblock == nil {
		return system.NilRank, errors.New("nil superblock while determining rank")
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
		// Addr member populated in msClient
	})
	if err != nil {
		return system.NilRank, err
	} else if resp.State == mgmtpb.JoinResp_OUT {
		return system.NilRank, errors.Errorf("rank %d excluded", resp.Rank)
	}
	r = system.Rank(resp.Rank)

	if !superblock.ValidRank {
		superblock.Rank = new(system.Rank)
		*superblock.Rank = r
		superblock.ValidRank = true
		srv.setSuperblock(superblock)
		if err := srv.WriteSuperblock(); err != nil {
			return system.NilRank, err
		}
	}

	return r, nil
}

// setRank determines the instance rank and sends a SetRank dRPC request
// to the IOServer.
func (srv *IOServerInstance) setRank(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	r, err := srv.determineRank(ctx, ready)
	if err != nil {
		return err
	}

	if err := srv.callSetRank(ctx, r); err != nil {
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
		return errors.Wrap(err, "unmarshall SetRank response")
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
	srv.runner.GetConfig().TargetCount = numTargets
}

// startMgmtSvc starts the DAOS management service replica associated
// with this instance. If no replica is associated with this instance, this
// function is a no-op.
func (srv *IOServerInstance) startMgmtSvc(ctx context.Context) error {
	superblock := srv.getSuperblock()

	// should have been loaded by now
	if superblock == nil {
		return errors.Errorf("%s instance %d: nil superblock", build.DataPlaneName, srv.Index())
	}

	if superblock.CreateMS {
		srv.log.Debugf("create MS (bootstrap=%t)", superblock.BootstrapMS)
		if err := srv.callCreateMS(ctx, superblock); err != nil {
			return err
		}
		superblock.CreateMS = false
		superblock.BootstrapMS = false
		srv.setSuperblock(superblock)
		if err := srv.WriteSuperblock(); err != nil {
			return err
		}
	}

	if superblock.MS {
		srv.log.Debug("start MS")
		if err := srv.callStartMS(ctx); err != nil {
			return err
		}

		msInfo, err := getMgmtInfo(srv)
		if err != nil {
			return err
		}
		if msInfo.isReplica {
			msg := "Management Service access point started"
			if msInfo.shouldBootstrap {
				msg += " (bootstrapped)"
			}
			srv.log.Info(msg)
		}
	}

	return nil
}

// loadModules initiates the I/O server startup sequence.
func (srv *IOServerInstance) loadModules(ctx context.Context) error {
	return srv.callSetUp(ctx)
}

func (srv *IOServerInstance) callCreateMS(ctx context.Context, superblock *Superblock) error {
	msAddr, err := srv.msClient.LeaderAddress()
	if err != nil {
		return err
	}
	req := &mgmtpb.CreateMsReq{}
	if superblock.BootstrapMS {
		req.Bootstrap = true
		req.Uuid = superblock.UUID
		req.Addr = msAddr
	}

	dresp, err := srv.CallDrpc(ctx, drpc.MethodCreateMS, req)
	if err != nil {
		return err
	}

	resp := &mgmtpb.DaosResp{}
	if err := proto.Unmarshal(dresp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal CreateMS response")
	}
	if resp.Status != 0 {
		return errors.Errorf("CreateMS: %d\n", resp.Status)
	}

	return nil
}

func (srv *IOServerInstance) callStartMS(ctx context.Context) error {
	dresp, err := srv.CallDrpc(ctx, drpc.MethodStartMS, nil)
	if err != nil {
		return err
	}

	resp := &mgmtpb.DaosResp{}
	if err := proto.Unmarshal(dresp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal StartMS response")
	}
	if resp.Status != 0 {
		return errors.Errorf("StartMS: %d\n", resp.Status)
	}

	return nil
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

// newMember returns reference to a new member struct if one can be retrieved
// from superblock, error otherwise. Member populated with local reply address.
func (srv *IOServerInstance) newMember() (*system.Member, error) {
	if !srv.hasSuperblock() {
		return nil, errors.New("missing superblock")
	}
	sb := srv.getSuperblock()

	msAddr, err := srv.msClient.LeaderAddress()
	if err != nil {
		return nil, err
	}

	addr, err := net.ResolveTCPAddr("tcp", msAddr)
	if err != nil {
		return nil, err
	}

	rank, err := srv.GetRank()
	if err != nil {
		return nil, err
	}

	return system.NewMember(rank, sb.UUID, sb.URI, addr, system.MemberStateJoined), nil
}
