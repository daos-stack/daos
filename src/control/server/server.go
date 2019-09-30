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
	"syscall"

	"github.com/pkg/errors"
	"google.golang.org/grpc"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// define supported maximum number of I/O servers
const maxIoServers = 1

// Start is the entry point for a daos_server instance.
func Start(log *logging.LeveledLogger, cfg *Configuration) error {
	// FIXME(mjmac): Temporarily set a global logger
	// until we get the dependency injection correct.
	level := log.Level()
	logging.SetLogger(log)
	// We have to set the global level because by default
	// it's based on the previous logger's level.
	logging.SetLevel(level)

	log.Debugf("cfg: %#v", cfg)

	// Backup active config.
	saveActiveConfig(log, cfg)

	// Create the root context here. All contexts should
	// inherit from this one so that they can be shut down
	// from one place.
	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	controlAddr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("0.0.0.0:%d", cfg.ControlPort))
	if err != nil {
		return errors.Wrap(err, "unable to resolve daos_server control address")
	}

	harness := NewIOServerHarness(&ext{}, log)
	for i, srvCfg := range cfg.Servers {
		if i+1 > maxIoServers {
			break
		}

		bp, err := storage.NewBdevProvider(log, srvCfg.Storage.SCM.MountPoint, &srvCfg.Storage.Bdev)
		if err != nil {
			return err
		}

		msClient := newMgmtSvcClient(ctx, log, mgmtSvcClientCfg{
			AccessPoints:    cfg.AccessPoints,
			ControlAddr:     controlAddr,
			TransportConfig: cfg.TransportConfig,
		})

		srv := NewIOServerInstance(harness.ext, log, bp, msClient, ioserver.NewRunner(log, srvCfg))
		if err := harness.AddInstance(srv); err != nil {
			return err
		}
	}

	// Single daos_server dRPC server to handle all iosrv requests
	if err := drpcSetup(cfg.SocketDir, harness.Instances(), cfg.TransportConfig); err != nil {
		return errors.WithMessage(err, "dRPC setup")
	}

	// Create and setup control service.
	controlService, err := NewControlService(log, harness, cfg)
	if err != nil {
		return errors.Wrap(err, "init control service")
	}
	if err := controlService.Setup(); err != nil {
		return errors.Wrap(err, "setup control service")
	}
	defer controlService.Teardown()

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

	// If running as root and user name specified in config file, respawn proc.
	needsRespawn := syscall.Getuid() == 0 && cfg.UserName != ""

	// Only provide IO/Agent communication if not attempting to respawn after format,
	// otherwise, only provide gRPC mgmt control service for hardware provisioning.
	if !needsRespawn {
		mgmtpb.RegisterMgmtSvcServer(grpcServer, newMgmtSvc(harness))
	}

	go func() {
		_ = grpcServer.Serve(lis)
	}()
	defer grpcServer.GracefulStop()

	log.Infof("DAOS control server listening on %s", controlAddr)

	sigChan := make(chan os.Signal)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGQUIT, syscall.SIGTERM)
	go func() {
		for {
			select {
			case sig := <-sigChan:
				log.Debugf("Caught signal: %s", sig)
				shutdown()
			}
		}
	}()

	// If running as root, wait for an indication that all instance
	// storage is ready and available. In the event that storage needs
	// to be formatted, it will block until a storage format request
	// is received by the management API.
	if syscall.Getuid() == 0 {
		if err := harness.AwaitStorageReady(ctx); err != nil {
			return err
		}
	}
	recreate := false // TODO: make this configurable
	if err := harness.CreateSuperblocks(recreate); err != nil {
		return err
	}

	if needsRespawn {
		// Chown required files and respawn process under new user.
		if err := changeFileOwnership(cfg); err != nil {
			return errors.WithMessage(err, "changing file ownership")
		}

		log.Infof("formatting complete and file ownership changed,"+
			"please rerun %s as user %s\n", os.Args[0], cfg.UserName)

		return nil
	}

	return errors.Wrap(harness.Start(ctx), "DAOS I/O Server exited with error")
}
