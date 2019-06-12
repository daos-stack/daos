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

package main

import (
	"fmt"
	"net"
	"os"
	"os/signal"
	"syscall"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/ioserver"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/daos-stack/daos/src/control/security/acl"
	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
)

func main() {
	if err := serverMain(); err != nil {
		log.Errorf("%+v\n", err)
		os.Exit(1)
	}
}

func parseCliOpts(opts *cliOptions) error {
	p := flags.NewParser(opts, flags.Default)
	// Continue with main if no subcommand is executed.
	p.SubcommandsOptional = true

	// Parse commandline flags which override options loaded from config.
	_, err := p.Parse()
	if err != nil {
		return err
	}

	return nil
}

// Temporary bridge between existing configuration and scoped configuration
func createIoserverConfigs(cfg *configuration) []*ioserver.Config {
	configs := make([]*ioserver.Config, 0, len(cfg.Servers))

	for _, cfgSrv := range cfg.Servers {
		rank := uint32(nilRank)
		if cfgSrv.Rank != nil {
			rank = uint32(*cfgSrv.Rank)
		}
		instanceCfg := ioserver.NewConfig().
			WithServerGroup(cfg.SystemName).
			WithRank(rank).
			WithStoragePath(cfgSrv.ScmMount).
			WithXSHelperCount(cfgSrv.NrXsHelpers).
			WithSocketDir(cfg.SocketDir).
			WithTargetCount(cfgSrv.Targets).
			WithSharedSegmentID(cfg.NvmeShmID).
			WithCartProvider(cfg.Provider).
			WithFabricInterface(cfgSrv.FabricIface).
			WithLogMask(cfgSrv.LogMask).
			WithLogFile(cfgSrv.LogFile)
		// temporary hack to deal with hard-coded env goop in config
		instanceCfg = instanceCfg.
			WithMetadataCap(1024).
			WithCartContextShareAddress(0).
			WithCartTimeout(30).
			WithDebugSubsystems("all")
		configs = append(configs, instanceCfg)
	}

	return configs
}

func serverMain() error {
	// Bootstrap default logger before config options get set.
	log.NewDefaultLogger(log.Debug, "", os.Stderr)

	opts := new(cliOptions)
	if err := parseCliOpts(opts); err != nil {
		return err
	}

	host, err := os.Hostname()
	if err != nil {
		log.Errorf("Failed to get hostname: %+v", err)
		return err
	}

	// Parse configuration file and load values.
	config, err := loadConfigOpts(opts, host)
	if err != nil {
		log.Errorf("Failed to load config options: %+v", err)
		return err
	}

	// Backup active config.
	saveActiveConfig(&config)

	f, err := config.setLogging(host)
	if err != nil {
		log.Errorf("Failed to configure logging: %+v", err)
		return err
	}
	if f != nil {
		defer f.Close()
	}

	// Create and setup control service.
	mgmtCtlSvc, err := newControlService(
		&config, getDrpcClientConnection(config.SocketDir))
	if err != nil {
		log.Errorf("Failed to init ControlService: %s", err)
		return err
	}
	mgmtCtlSvc.Setup()
	defer mgmtCtlSvc.Teardown()

	// Create and start listener on management network.
	addr := fmt.Sprintf("0.0.0.0:%d", config.Port)
	lis, err := net.Listen("tcp4", addr)
	if err != nil {
		log.Errorf("Unable to listen on management interface: %s", err)
		return err
	}
	log.Debugf("DAOS control server listening on %s", addr)

	// Create new grpc server, register services and start serving (after
	// dropping privileges).
	var sOpts []grpc.ServerOption
	// TODO: This will need to be extended to take certificate information for
	// the TLS protected channel. Currently it is an "insecure" channel.
	grpcServer := grpc.NewServer(sOpts...)

	mgmtpb.RegisterMgmtCtlServer(grpcServer, mgmtCtlSvc)
	mgmtpb.RegisterMgmtSvcServer(grpcServer, newMgmtSvc(&config))
	secServer := newSecurityService(getDrpcClientConnection(config.SocketDir))
	acl.RegisterAccessControlServer(grpcServer, secServer)

	go grpcServer.Serve(lis)
	defer grpcServer.GracefulStop()

	// Wait for storage to be formatted if necessary and subsequently drop
	// current process privileges to that of normal user.
	if err = awaitStorageFormat(&config); err != nil {
		log.Errorf("Failed to format storage: %s", err)
		return err
	}

	harness := NewIOServerHarness()
	for i, instanceConfig := range createIoserverConfigs(&config) {
		// only start one for now
		if i > 0 {
			break
		}
		harness.AddInstance(instanceConfig)
	}

	if err = drpcSetup(config.SocketDir, harness); err != nil {
		log.Errorf("Failed to set up dRPC: %s", err)
		return err
	}

	ctx, shutdown := context.WithCancel(context.Background())
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

	log.Debugf("Starting DAOS I/O Server")
	return errors.Wrap(harness.StartInstances(ctx), "DAOS Server exited abnormally")
	/*
		// Format the unformatted servers by writing persistant superblock.
		if err = formatIosrvs(&config, false); err != nil {
			log.Errorf("Failed to format servers: %s", err)
			return err
		}

		// Only start single io_server for now.
		// TODO: Extend to start two io_servers per host.
		iosrv, err := newIosrv(&config, 0)
		if err != nil {
			log.Errorf("Failed to load server: %s", err)
			return err
		}
		if err = drpcSetup(config.SocketDir, iosrv); err != nil {
			log.Errorf("Failed to set up dRPC: %s", err)
			return err
		}
		if err = iosrv.start(); err != nil {
			log.Errorf("Failed to start server: %s", err)
			return err
		}

		extraText, err := CheckReplica(lis, config.AccessPoints, iosrv.cmd)
		if err != nil {
			log.Errorf(
				"Unable to determine if management service replica: %s",
				err)
			return err
		}
		log.Debugf("DAOS I/O server running %s", extraText)

		// Wait for I/O server to return.
		err = iosrv.wait()
		if err != nil {
			log.Errorf("DAOS I/O server exited with error: %s", err)
		}

		return err*/
}
