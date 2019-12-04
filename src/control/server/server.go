//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"os/signal"
	"os/user"
	"strings"
	"syscall"

	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

const (
	ControlPlaneName = "DAOS Control Server"
	DataPlaneName    = "DAOS I/O Server"
)

func cfgHasBdev(cfg *Configuration) bool {
	for _, srvCfg := range cfg.Servers {
		if len(srvCfg.Storage.Bdev.DeviceList) > 0 {
			return true
		}
	}

	return false
}

// define supported maximum number of I/O servers
const maxIoServers = 2

// Start is the entry point for a daos_server instance.
func Start(log *logging.LeveledLogger, cfg *Configuration) error {
	err := cfg.Validate()
	if err != nil {
		return errors.Wrapf(err, "%s: validation failed", cfg.Path)
	}

	// Backup active config.
	saveActiveConfig(log, cfg)

	if cfg.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cfg.HelperLogFile); err != nil {
			return errors.Wrap(err, "unable to configure privileged helper logging")
		}
	}

	// Create the root context here. All contexts should
	// inherit from this one so that they can be shut down
	// from one place.
	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	controlAddr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("0.0.0.0:%d", cfg.ControlPort))
	if err != nil {
		return errors.Wrap(err, "unable to resolve daos_server control address")
	}

	if len(cfg.Servers) > 1 && cfgHasBdev(cfg) {
		return errors.New("NVMe support only available with single server in this release")
	}

	// If this daos_server instance ends up being the MS leader,
	// this will record the DAOS system membership.
	membership := common.NewMembership(log)
	scmProvider := scm.DefaultProvider(log)
	bdevProvider := bdev.DefaultProvider(log)
	harness := NewIOServerHarness(log)
	for i, srvCfg := range cfg.Servers {
		if i+1 > maxIoServers {
			break
		}

		bp, err := bdev.NewClassProvider(log, srvCfg.Storage.SCM.MountPoint, &srvCfg.Storage.Bdev)
		if err != nil {
			return err
		}

		msClient := newMgmtSvcClient(ctx, log, mgmtSvcClientCfg{
			AccessPoints:    cfg.AccessPoints,
			ControlAddr:     controlAddr,
			TransportConfig: cfg.TransportConfig,
		})

		srv := NewIOServerInstance(log, bp, scmProvider, msClient, ioserver.NewRunner(log, srvCfg))
		if err := harness.AddInstance(srv); err != nil {
			return err
		}
	}

	runningUser, err := user.Current()
	if err != nil {
		return errors.Wrap(err, "unable to lookup current user")
	}
	// Perform an automatic prepare based on the values in the config file.
	// TODO (DAOS-3404): Move this into the loop above to perform a prepare
	// for each ioserver.
	prepReq := bdev.PrepareRequest{
		HugePageCount: cfg.NrHugepages,
		TargetUser:    runningUser.Username,
		PCIWhitelist:  strings.Join(cfg.BdevInclude, ","),
	}
	if _, err := bdevProvider.Prepare(prepReq); err != nil {
		log.Errorf("automatic NVMe prepare failed (check configuration?)\n%s", err)
	}

	// Single daos_server dRPC server to handle all iosrv requests
	if err := drpcSetup(ctx, log, cfg.SocketDir, harness.Instances(), cfg.TransportConfig); err != nil {
		return errors.WithMessage(err, "dRPC setup")
	}

	// Create and setup control service.
	controlService, err := NewControlService(log, harness, bdevProvider, scmProvider, cfg, membership)
	if err != nil {
		return errors.Wrap(err, "init control service")
	}
	if err := controlService.Setup(); err != nil {
		return errors.Wrap(err, "setup control service")
	}

	// Create and start listener on management network.
	lis, err := net.Listen("tcp4", controlAddr.String())
	if err != nil {
		return errors.Wrap(err, "unable to listen on management interface")
	}

	// Create new grpc server, register services and start serving.
	tcOpt, err := security.ServerOptionForTransportConfig(cfg.TransportConfig)
	if err != nil {
		return err
	}

	grpcServer := grpc.NewServer(tcOpt)
	ctlpb.RegisterMgmtCtlServer(grpcServer, controlService)
	mgmtpb.RegisterMgmtSvcServer(grpcServer, newMgmtSvc(harness, membership))

	go func() {
		_ = grpcServer.Serve(lis)
	}()
	defer grpcServer.GracefulStop()

	log.Infof("%s (pid %d) listening on %s", ControlPlaneName, os.Getpid(), controlAddr)

	sigChan := make(chan os.Signal)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGQUIT, syscall.SIGTERM)
	go func() {
		sig := <-sigChan
		log.Debugf("Caught signal: %s", sig)
		if err := drpcCleanup(cfg.SocketDir); err != nil {
			log.Errorf("error during dRPC cleanup: %s", err)
		}
		shutdown()
	}()

	if err := harness.AwaitStorageReady(ctx, cfg.RecreateSuperblocks); err != nil {
		return err
	}

	if err := harness.CreateSuperblocks(cfg.RecreateSuperblocks); err != nil {
		return err
	}

	return errors.Wrapf(harness.Start(ctx), "%s exited with error", DataPlaneName)
}
