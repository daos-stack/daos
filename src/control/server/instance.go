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

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// IOServerInstance encapsulates control-plane specific configuration
// and functionality for managed I/O server instances. The distinction
// between this structure and what's in the ioserver package is that the
// ioserver package is only concerned with configuring and executing
// a single daos_io_server instance. IOServerInstance is intended to
// be used with IOServerHarness to manage and monitor multiple instances
// per node.
type IOServerInstance struct {
	Index int

	ext           External
	log           logging.Logger
	runner        *ioserver.Runner
	bdevProvider  *storage.BdevProvider
	superblock    *Superblock
	drpcClient    drpc.DomainSocketClient
	msClient      *mgmtSvcClient
	instanceReady chan *srvpb.NotifyReadyReq
	storageReady  chan struct{}
	fsRoot        string
}

// NewIOServerInstance returns an *IOServerInstance initialized with
// its dependencies.
func NewIOServerInstance(ext External, log logging.Logger,
	bp *storage.BdevProvider, msc *mgmtSvcClient, r *ioserver.Runner) *IOServerInstance {

	return &IOServerInstance{
		Index:         r.Config.Index,
		ext:           ext,
		log:           log,
		runner:        r,
		bdevProvider:  bp,
		msClient:      msc,
		drpcClient:    getDrpcClientConnection(r.Config.SocketDir),
		instanceReady: make(chan *srvpb.NotifyReadyReq),
		storageReady:  make(chan struct{}),
	}
}

// MountScmDevice mounts the configured SCM device (DCPM or ramdisk emulation)
// at the mountpoint specified in the configuration. If the device is already
// mounted, the function returns nil, indicating success.
func (srv *IOServerInstance) MountScmDevice() error {
	scmCfg := srv.runner.Config.Storage.SCM

	isMount, err := srv.ext.isMountPoint(scmCfg.MountPoint)
	if err != nil && !os.IsNotExist(errors.Cause(err)) {
		return errors.WithMessage(err, "failed to check SCM mount")
	}
	if isMount {
		srv.log.Debugf("%s already mounted", scmCfg.MountPoint)
		return nil
	}

	srv.log.Debugf("attempting to mount existing SCM dir %s\n", scmCfg.MountPoint)
	mntType, devPath, mntOpts, err := getMntParams(scmCfg)
	if err != nil {
		return errors.WithMessage(err, "getting scm mount params")
	}

	srv.log.Debugf("mounting scm %s at %s (%s)...", devPath, scmCfg.MountPoint, mntType)
	if err := srv.ext.mount(devPath, scmCfg.MountPoint, mntType, uintptr(0), mntOpts); err != nil {
		return errors.WithMessage(err, "mounting existing scm dir")
	}

	return nil
}

// Start checks to make sure that the instance has a valid superblock before
// performing any required NVMe preparation steps and launching a managed
// daos_io_server instance.
func (srv *IOServerInstance) Start(ctx context.Context, errChan chan<- error) error {
	if srv.superblock == nil {
		if err := srv.ReadSuperblock(); err != nil {
			return errors.Wrap(err, "start failed; no superblock")
		}
	}
	if err := srv.bdevProvider.PrepareDevices(); err != nil {
		return errors.Wrap(err, "start failed; unable to prepare NVMe device(s)")
	}
	if err := srv.bdevProvider.GenConfigFile(); err != nil {
		return errors.Wrap(err, "start failed; unable to generate NVMe configuration for SPDK")
	}

	return srv.runner.Start(ctx, errChan)
}

// NotifyReady receives a ready message from the running IOServer
// instance.
func (srv *IOServerInstance) NotifyReady(msg *srvpb.NotifyReadyReq) {
	srv.log.Debugf("I/O server instance %d ready: %v", srv.Index, msg)

	go func() {
		srv.instanceReady <- msg
	}()
}

// AwaitReady returns a channel which receives a ready message
// when the started IOServer instance indicates that it is
// ready to receive dRPC messages.
func (srv *IOServerInstance) AwaitReady() chan *srvpb.NotifyReadyReq {
	return srv.instanceReady
}

// NotifyStorageReady releases any blocks on AwaitStorageReady().
func (srv *IOServerInstance) NotifyStorageReady() {
	srv.log.Debugf("I/O server instance %d notifying storage ready", srv.Index)
	go func() {
		close(srv.storageReady)
	}()
}

// AwaitStorageReady blocks until the IOServer's storage is ready.
func (srv *IOServerInstance) AwaitStorageReady(ctx context.Context) {
	select {
	case <-ctx.Done():
		srv.log.Infof("I/O server instance %d storage not ready: %s", srv.Index, ctx.Err())
	case <-srv.storageReady:
		srv.log.Infof("I/O server instance %d storage ready", srv.Index)
	}
}

// SetRank determines the instance rank and sends a SetRank dRPC request
// to the IOServer.
func (srv *IOServerInstance) SetRank(ctx context.Context, ready *srvpb.NotifyReadyReq) error {
	r := ioserver.NilRank
	if srv.superblock.Rank != nil {
		r = *srv.superblock.Rank
	}

	if !srv.superblock.ValidRank || !srv.superblock.MS {
		resp, err := srv.msClient.Join(ctx, &mgmtpb.JoinReq{
			Uuid:  srv.superblock.UUID,
			Rank:  uint32(r),
			Uri:   ready.Uri,
			Nctxs: ready.Nctxs,
		})
		if err != nil {
			return err
		} else if resp.State == mgmtpb.JoinResp_OUT {
			return errors.Errorf("rank %d excluded", resp.Rank)
		}
		r = ioserver.Rank(resp.Rank)

		if !srv.superblock.ValidRank {
			srv.superblock.Rank = new(ioserver.Rank)
			*srv.superblock.Rank = r
			srv.superblock.ValidRank = true
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

func (srv *IOServerInstance) callSetRank(rank ioserver.Rank) error {
	dresp, err := makeDrpcCall(srv.drpcClient, mgmtModuleID, setRank, &mgmtpb.SetRankReq{Rank: uint32(rank)})
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

// StartManagementService starts the DAOS management service replica associated
// with this instance. If no replica is associated with this instance, this
// function is a no-op.
func (srv *IOServerInstance) StartManagementService() error {
	// should have been loaded by now
	if srv.superblock == nil {
		return errors.Errorf("I/O server instance %d: nil superblock", srv.Index)
	}

	if srv.superblock.CreateMS {
		srv.log.Debugf("create MS (bootstrap=%t)", srv.superblock.BootstrapMS)
		if err := srv.callCreateMS(); err != nil {
			return err
		}
		srv.superblock.CreateMS = false
		srv.superblock.BootstrapMS = false
		if err := srv.WriteSuperblock(); err != nil {
			return err
		}
	}

	if srv.superblock.MS {
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

	// Notify the I/O server that it may set up its server modules now.
	return srv.callSetUp()
}

func (srv *IOServerInstance) callCreateMS() error {
	msAddr, err := srv.msClient.LeaderAddress()
	if err != nil {
		return err
	}
	req := &mgmtpb.CreateMsReq{}
	if srv.superblock.BootstrapMS {
		req.Bootstrap = true
		req.Uuid = srv.superblock.UUID
		req.Addr = msAddr
	}

	dresp, err := makeDrpcCall(srv.drpcClient, mgmtModuleID, createMS, req)
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
	dresp, err := makeDrpcCall(srv.drpcClient, mgmtModuleID, startMS, nil)
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
	dresp, err := makeDrpcCall(srv.drpcClient, mgmtModuleID, setUp, nil)
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
