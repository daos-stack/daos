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
	"bytes"
	"fmt"
	"net"
	"os"
	"os/exec"
	"syscall"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/daos-stack/daos/src/control/security/acl"
	flags "github.com/jessevdk/go-flags"
	"google.golang.org/grpc"
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

	ctrlLogFile, err := config.setLogging(host)
	if err != nil {
		log.Errorf("Failed to configure logging: %+v", err)
		return err
	}
	if ctrlLogFile != nil {
		defer ctrlLogFile.Close()
	}

	// Create and setup control service.
	mgmtCtlSvc, err := newControlService(
		&config, getDrpcClientConnection(config.SocketDir))
	if err != nil {
		log.Errorf("Failed to init ControlService: %+v", err)
		return err
	}
	mgmtCtlSvc.Setup()
	defer mgmtCtlSvc.Teardown()

	// Create and start listener on management network.
	addr := fmt.Sprintf("0.0.0.0:%d", config.Port)
	lis, err := net.Listen("tcp4", addr)
	if err != nil {
		log.Errorf("Unable to listen on management interface: %+v", err)
		return err
	}
	log.Debugf("DAOS control server listening on %s", addr)

	// Create new grpc server, register services and start serving.
	var sOpts []grpc.ServerOption
	// TODO: This will need to be extended to take certificate information for
	// the TLS protected channel. Currently it is an "insecure" channel.
	grpcServer := grpc.NewServer(sOpts...)

	mgmtpb.RegisterMgmtCtlServer(grpcServer, mgmtCtlSvc)

	// If running as root and user name specified in config file, respawn proc.
	respawn := syscall.Getuid() == 0 && config.UserName != ""

	// Only provide IO/Agent communication if not attempting to respawn after format,
	// otherwise, only provide gRPC mgmt control service for hardware provisioning.
	if !respawn {
		mgmtpb.RegisterMgmtSvcServer(grpcServer, newMgmtSvc(&config))
		secServer := newSecurityService(getDrpcClientConnection(config.SocketDir))
		acl.RegisterAccessControlServer(grpcServer, secServer)
	}

	go func() {
		_ = grpcServer.Serve(lis)
	}()
	defer grpcServer.GracefulStop()

	// If running as root, wait for storage format call over client API (mgmt tool).
	if syscall.Getuid() == 0 {
		if err = awaitStorageFormat(&config); err != nil {
			log.Errorf("Failed to format storage: %+v", err)
			return err
		}
	}

	if respawn {
		// Chown required files and respawn process under new user.
		if err := changeFileOwnership(&config); err != nil {
			log.Errorf("Failed to change file ownership: %+v", err)
			return err
		}

		var buf bytes.Buffer // build command string

		// Wait for this proc to exit and make NVMe storage accessible to new user.
		fmt.Fprintf(
			&buf, `sleep 1 && %s storage prep-nvme -u %s &> %s`,
			os.Args[0], config.UserName, config.ControlLogFile)

		// Run daos_server from within a subshell of target user with the same args.
		fmt.Fprintf(&buf, ` && su %s -c "`, config.UserName)

		for _, arg := range os.Args {
			fmt.Fprintf(&buf, arg+" ")
		}

		// Redirect output of new proc to existing log file.
		fmt.Fprintf(&buf, `&> %s"`, config.ControlLogFile)

		msg := fmt.Sprintf("dropping privileges: re-spawning (%s)\n", buf.String())
		log.Debugf(msg)

		if err := exec.Command("bash", "-c", buf.String()).Start(); err != nil {
			log.Errorf("Failed to respawn: %+v", err)
			return err
		}

		return nil
	}

	// Format the unformatted servers by writing persistant superblock.
	if err = formatIosrvs(&config, false); err != nil {
		log.Errorf("Failed to format servers: %+v", err)
		return err
	}

	// Only start single io_server for now.
	// TODO: Extend to start two io_servers per host.
	iosrv, err := newIosrv(&config, 0)
	if err != nil {
		log.Errorf("Failed to load server: %+v", err)
		return err
	}
	if err = drpcSetup(config.SocketDir, iosrv); err != nil {
		log.Errorf("Failed to set up dRPC: %+v", err)
		return err
	}
	// Log iosrv std{err,out} (unformatted) to ctrl log to not pollute DAOS log.
	if err = iosrv.start(ctrlLogFile); err != nil {
		log.Errorf("Failed to start server: %+v", err)
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
		log.Errorf("DAOS I/O server exited with error: %+v", err)
	}

	return err
}
