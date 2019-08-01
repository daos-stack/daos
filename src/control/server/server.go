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
	"fmt"
	"net"
	"os"
	"syscall"

	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	log "github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/acl"
)

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

// Main is the current entry point into daos_server functionality
// TODO: Refactor this to decouple CLI functionality from core
// server logic and allow for easier testing.
func Main() error {

	opts := new(cliOptions)
	if err := parseCliOpts(opts); err != nil {
		return err
	}

	if opts.Debug {
		log.SetLevel(log.LogLevelDebug)
		log.Debug("debug output enabled")
	}
	if opts.JSON {
		log.SetJSONOutput()
		log.Info("JSON output enabled")
	}

	host, err := os.Hostname()
	if err != nil {
		return err
	}

	// Parse configuration file and load values.
	config, err := loadConfigOpts(opts, host)
	if err != nil {
		return errors.WithMessage(err, "load config options")
	}

	// Backup active config.
	saveActiveConfig(config)

	ctlLogFile, err := config.setLogging(host, opts.JSON)
	if err != nil {
		return errors.Wrap(err, "configure logging")
	}
	if ctlLogFile != nil {
		defer ctlLogFile.Close()
	}

	// Create and setup control service.
	mgmtCtlSvc, err := newControlService(
		config, getDrpcClientConnection(config.SocketDir))
	if err != nil {
		return errors.Wrap(err, "init control server")
	}
	mgmtCtlSvc.Setup()
	defer mgmtCtlSvc.Teardown()

	// Create and start listener on management network.
	addr := fmt.Sprintf("0.0.0.0:%d", config.Port)
	lis, err := net.Listen("tcp4", addr)
	if err != nil {
		return errors.Wrap(err, "unable to listen on management interface")
	}
	log.Infof("DAOS control server listening on %s", addr)

	// Create new grpc server, register services and start serving.
	var sOpts []grpc.ServerOption

	opt, err := security.ServerOptionForTransportConfig(config.TransportConfig)
	if err != nil {
		return err
	}
	sOpts = append(sOpts, opt)

	grpcServer := grpc.NewServer(sOpts...)

	mgmtpb.RegisterMgmtCtlServer(grpcServer, mgmtCtlSvc)

	// If running as root and user name specified in config file, respawn proc.
	needsRespawn := syscall.Getuid() == 0 && config.UserName != ""

	// Only provide IO/Agent communication if not attempting to respawn after format,
	// otherwise, only provide gRPC mgmt control service for hardware provisioning.
	if !needsRespawn {
		mgmtpb.RegisterMgmtSvcServer(grpcServer, newMgmtSvc(config))
		secServer := newSecurityService(getDrpcClientConnection(config.SocketDir))
		acl.RegisterAccessControlServer(grpcServer, secServer)
	}

	go func() {
		_ = grpcServer.Serve(lis)
	}()
	defer grpcServer.GracefulStop()

	// If running as root, wait for storage format call over client API (mgmt tool).
	if syscall.Getuid() == 0 {
		if err = awaitStorageFormat(config); err != nil {
			return errors.Wrap(err, "format storage")
		}
	}

	if needsRespawn {
		// Chown required files and respawn process under new user.
		if err := changeFileOwnership(config); err != nil {
			return errors.WithMessage(err, "changing file ownership")
		}

		log.Infof("formatting complete and file ownership changed,"+
			"please rerun %s as user %s\n", os.Args[0], config.UserName)

		return nil
	}

	// Format the unformatted servers by writing persistant superblock.
	if err = formatIosrvs(config, false); err != nil {
		return errors.Wrap(err, "format servers")
	}

	// Only start single io_server for now.
	// TODO: Extend to start two io_servers per host.
	iosrv, err := newIosrv(config, 0)
	if err != nil {
		return errors.WithMessage(err, "load server")
	}
	if err = drpcSetup(config.SocketDir, iosrv, config.TransportConfig); err != nil {
		return errors.WithMessage(err, "set up dRPC")
	}
	if err = iosrv.start(); err != nil {
		return errors.WithMessage(err, "start server")
	}

	extraText, err := CheckReplica(lis, config.AccessPoints, iosrv.cmd)
	if err != nil {
		return errors.Wrap(err, "unable to determine if management service replica")
	}
	log.Infof("DAOS I/O server running %s", extraText)

	// Wait for I/O server to return.
	err = iosrv.wait()
	if err != nil {
		return errors.WithMessage(err, "DAOS I/O server exited with error")
	}

	return err
}
