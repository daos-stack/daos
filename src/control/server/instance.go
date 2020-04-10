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

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

// IOServerRunner defines an interface for starting and stopping the
// daos_io_server.
type IOServerRunner interface {
	Start(context.Context, chan<- ioserver.InstanceError) error
	IsRunning() bool
	Signal(os.Signal) error
	Wait() error
	GetConfig() *ioserver.Config
}

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
	waitDrpc          atm.Bool
	drpcReady         chan *srvpb.NotifyReadyReq
	storageReady      chan struct{}
	ready             atm.Bool
	fsRoot            string

	sync.RWMutex
	// these must be protected by a mutex in order to
	// avoid racy access.
	_drpcClient   drpc.DomainSocketClient
	_scmStorageOk bool // cache positive result of NeedsStorageFormat()
	_superblock   *Superblock
	_lastErr      error // populated when harness receives signal
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
		storageReady:      make(chan struct{}),
	}
}

// IsReady indicates whether the IOServerInstance is in a ready state.
//
// If true indicates that the instance is fully setup, distinct from
// drpc and storage ready states, and currently active.
func (srv *IOServerInstance) IsReady() bool {
	return srv.ready.IsTrue() && srv.IsStarted()
}

// scmConfig returns the scm configuration assigned to this instance.
func (srv *IOServerInstance) scmConfig() storage.ScmConfig {
	return srv.runner.GetConfig().Storage.SCM
}

// bdevConfig returns the block device configuration assigned to this instance.
func (srv *IOServerInstance) bdevConfig() storage.BdevConfig {
	return srv.runner.GetConfig().Storage.Bdev
}

func (srv *IOServerInstance) setDrpcClient(c drpc.DomainSocketClient) {
	srv.Lock()
	defer srv.Unlock()
	srv._drpcClient = c
}

func (srv *IOServerInstance) getDrpcClient() (drpc.DomainSocketClient, error) {
	srv.RLock()
	defer srv.RUnlock()
	if srv._drpcClient == nil {
		return nil, errors.New("no dRPC client set (data plane not started?)")
	}
	return srv._drpcClient, nil
}

// SetIndex sets the server index assigned by the harness.
func (srv *IOServerInstance) SetIndex(idx uint32) {
	srv.runner.GetConfig().Index = idx
}

// Index returns the server index assigned by the harness.
func (srv *IOServerInstance) Index() uint32 {
	return srv.runner.GetConfig().Index
}

// MountScmDevice mounts the configured SCM device (DCPM or ramdisk emulation)
// at the mountpoint specified in the configuration. If the device is already
// mounted, the function returns nil, indicating success.
func (srv *IOServerInstance) MountScmDevice() error {
	scmCfg := srv.scmConfig()

	isMount, err := srv.scmProvider.IsMounted(scmCfg.MountPoint)
	if err != nil && !os.IsNotExist(errors.Cause(err)) {
		return errors.WithMessage(err, "failed to check SCM mount")
	}
	if isMount {
		return nil
	}

	srv.log.Debugf("attempting to mount existing SCM dir %s\n", scmCfg.MountPoint)

	var res *scm.MountResponse
	switch scmCfg.Class {
	case storage.ScmClassRAM:
		res, err = srv.scmProvider.MountRamdisk(scmCfg.MountPoint, uint(scmCfg.RamdiskSize))
	case storage.ScmClassDCPM:
		if len(scmCfg.DeviceList) != 1 {
			err = scm.FaultFormatInvalidDeviceCount
			break
		}
		res, err = srv.scmProvider.MountDcpm(scmCfg.DeviceList[0], scmCfg.MountPoint)
	default:
		err = errors.New(scm.MsgScmClassNotSupported)
	}
	if err != nil {
		return errors.WithMessage(err, "mounting existing scm dir")
	}
	srv.log.Debugf("%s mounted: %t", res.Target, res.Mounted)

	return nil
}

// NeedsScmFormat probes the configured instance storage and determines whether
// or not it requires a format operation before it can be used.
func (srv *IOServerInstance) NeedsScmFormat() (bool, error) {
	srv.RLock()
	if srv._scmStorageOk {
		srv.RUnlock()
		return false, nil
	}
	srv.RUnlock()

	scmCfg := srv.scmConfig()

	srv.log.Debugf("%s: checking formatting", scmCfg.MountPoint)

	// take a lock here to ensure that we can safely set _scmStorageOk
	// as well as avoiding racy access to stuff in srv.ext.
	srv.Lock()
	defer srv.Unlock()

	req, err := scm.CreateFormatRequest(scmCfg, false)
	if err != nil {
		return false, err
	}

	res, err := srv.scmProvider.CheckFormat(*req)
	if err != nil {
		return false, err
	}

	needsFormat := !res.Mounted && !res.Mountable
	srv.log.Debugf("%s (%s) needs format: %t", scmCfg.MountPoint, scmCfg.Class, needsFormat)
	return needsFormat, nil
}

// Start checks to make sure that the instance has a valid superblock before
// performing any required NVMe preparation steps and launching a managed
// daos_io_server instance.
func (srv *IOServerInstance) Start(ctx context.Context, errChan chan<- ioserver.InstanceError) error {
	if !srv.hasSuperblock() {
		if err := srv.ReadSuperblock(); err != nil {
			return errors.Wrap(err, "start failed; no superblock")
		}
	}
	if err := srv.bdevClassProvider.PrepareDevices(); err != nil {
		return errors.Wrap(err, "start failed; unable to prepare NVMe device(s)")
	}
	if err := srv.bdevClassProvider.GenConfigFile(); err != nil {
		return errors.Wrap(err, "start failed; unable to generate NVMe configuration for SPDK")
	}

	if err := srv.logScmStorage(); err != nil {
		srv.log.Errorf("unable to log SCM storage stats: %s", err)
	}

	return srv.runner.Start(ctx, errChan)
}

// Stop sends signal to stop IOServerInstance runner (but doesn't wait for
// process to exit).
func (srv *IOServerInstance) Stop(signal os.Signal) error {
	if err := srv.runner.Signal(signal); err != nil {
		return err
	}

	return nil
}

// RemoveSocket removes the socket file used for dRPC communication with
// harness and updates relevant ready states.
func (srv *IOServerInstance) RemoveSocket() error {
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

	// reset state
	srv.ready.SetFalse()
	srv.drpcReady = make(chan *srvpb.NotifyReadyReq)

	return nil
}

// IsStarted indicates whether IOServerInstance is in a running state.
func (srv *IOServerInstance) IsStarted() bool {
	return srv.runner.IsRunning()
}

// NotifyDrpcReady receives a ready message from the running IOServer
// instance.
func (srv *IOServerInstance) NotifyDrpcReady(msg *srvpb.NotifyReadyReq) {
	srv.log.Debugf("%s instance %d drpc ready: %v", DataPlaneName, srv.Index(), msg)

	// Activate the dRPC client connection to this iosrv
	srv.setDrpcClient(drpc.NewClientConnection(msg.DrpcListenerSock))

	go func() {
		srv.drpcReady <- msg
	}()
}

// AwaitDrpcReady returns a channel which receives a ready message
// when the started IOServer instance indicates that it is
// ready to receive dRPC messages.
func (srv *IOServerInstance) AwaitDrpcReady() chan *srvpb.NotifyReadyReq {
	return srv.drpcReady
}

// NotifyStorageReady releases any blocks on AwaitStorageReady().
func (srv *IOServerInstance) NotifyStorageReady() {
	go func() {
		close(srv.storageReady)
	}()
}

// AwaitStorageReady blocks until the IOServer's storage is ready.
func (srv *IOServerInstance) AwaitStorageReady(ctx context.Context) {
	select {
	case <-ctx.Done():
		srv.log.Infof("%s instance %d storage not ready: %s", DataPlaneName, srv.Index(), ctx.Err())
	case <-srv.storageReady:
		srv.log.Infof("%s instance %d storage ready", DataPlaneName, srv.Index())
	}
}

// SetRank determines the instance rank and sends a SetRank dRPC request
// to the IOServer.
func (srv *IOServerInstance) SetRank(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	superblock := srv.getSuperblock()
	if superblock == nil {
		return errors.New("nil superblock in SetRank()")
	}

	r := system.NilRank
	if superblock.Rank != nil {
		r = *superblock.Rank
	}

	if !superblock.ValidRank || !superblock.MS {
		resp, err := srv.msClient.Join(ctx, &mgmtpb.JoinReq{
			Uuid:  superblock.UUID,
			Rank:  r.Uint32(),
			Uri:   ready.Uri,
			Nctxs: ready.Nctxs,
			// Addr member populated in msClient
		})
		if err != nil {
			return err
		} else if resp.State == mgmtpb.JoinResp_OUT {
			return errors.Errorf("rank %d excluded", resp.Rank)
		}
		r = system.Rank(resp.Rank)

		if !superblock.ValidRank {
			superblock.Rank = new(system.Rank)
			*superblock.Rank = r
			superblock.ValidRank = true
			srv.setSuperblock(superblock)
			if err := srv.WriteSuperblock(); err != nil {
				return err
			}
		}
	}

	if err := srv.callSetRank(r); err != nil {
		return err
	}

	return nil
}

func (srv *IOServerInstance) callSetRank(rank system.Rank) error {
	dresp, err := srv.CallDrpc(drpc.ModuleMgmt, drpc.MethodSetRank, &mgmtpb.SetRankReq{Rank: rank.Uint32()})
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

// SetTargetCount updates target count in ioserver config.
func (srv *IOServerInstance) SetTargetCount(numTargets int) {
	srv.runner.GetConfig().TargetCount = numTargets
}

// StartManagementService starts the DAOS management service replica associated
// with this instance. If no replica is associated with this instance, this
// function is a no-op.
func (srv *IOServerInstance) StartManagementService() error {
	superblock := srv.getSuperblock()

	// should have been loaded by now
	if superblock == nil {
		return errors.Errorf("%s instance %d: nil superblock", DataPlaneName, srv.Index())
	}

	if superblock.CreateMS {
		srv.log.Debugf("create MS (bootstrap=%t)", superblock.BootstrapMS)
		if err := srv.callCreateMS(superblock); err != nil {
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
		if err := srv.callStartMS(); err != nil {
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

// LoadModules initiates the I/O server startup sequence.
func (srv *IOServerInstance) LoadModules() error {
	return srv.callSetUp()
}

func (srv *IOServerInstance) callCreateMS(superblock *Superblock) error {
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

	dresp, err := srv.CallDrpc(drpc.ModuleMgmt, drpc.MethodCreateMS, req)
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

func (srv *IOServerInstance) callStartMS() error {
	dresp, err := srv.CallDrpc(drpc.ModuleMgmt, drpc.MethodStartMS, nil)
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

func (srv *IOServerInstance) callSetUp() error {
	dresp, err := srv.CallDrpc(drpc.ModuleMgmt, drpc.MethodSetUp, nil)
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

// IsMSReplica indicates whether or not this instance is a management service replica.
func (srv *IOServerInstance) IsMSReplica() bool {
	return srv.hasSuperblock() && srv.getSuperblock().MS
}

// CallDrpc makes the supplied dRPC call via this instance's dRPC client.
func (srv *IOServerInstance) CallDrpc(module, method int32, body proto.Message) (*drpc.Response, error) {
	dc, err := srv.getDrpcClient()
	if err != nil {
		return nil, err
	}

	return makeDrpcCall(dc, module, method, body)
}

// FinishStartup sets up instance once dRPC comms are ready, this includes
// setting the instance rank, starting management service and loading IO server
// modules.
//
// Instance ready state is set to indicate that all setup is complete.
func (srv *IOServerInstance) FinishStartup(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	if err := srv.SetRank(ctx, ready); err != nil {
		return err
	}
	// update ioserver target count to reflect allocated
	// number of targets, not number requested when starting
	srv.SetTargetCount(int(ready.GetNtgts()))

	if srv.IsMSReplica() {
		if err := srv.StartManagementService(); err != nil {
			return errors.Wrap(err, "failed to start management service")
		}
	}

	if err := srv.LoadModules(); err != nil {
		return errors.Wrap(err, "failed to load I/O server modules")
	}

	srv.ready.SetTrue()

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

	return system.NewMember(rank, sb.UUID, addr, system.MemberStateStarted), nil
}
