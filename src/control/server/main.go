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
	"runtime"
	"syscall"

	flags "github.com/jessevdk/go-flags"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	secpb "github.com/daos-stack/daos/src/control/security/proto"
)

// cliOptions struct defined flags that can be used when invoking daos_server.
type cliOptions struct {
	Port        uint16             `short:"p" long:"port" description:"Port for the gRPC management interfect to listen on"`
	MountPath   string             `short:"s" long:"storage" description:"Storage path"`
	ConfigPath  string             `short:"o" long:"config_path" description:"Server config file path"`
	Modules     *string            `short:"m" long:"modules" description:"List of server modules to load"`
	Cores       uint16             `short:"c" long:"cores" default:"0" description:"number of cores to use (default all)"`
	XShelpernr  int                `short:"x" long:"xshelpernr" default:"1" description:"number of helper XS per VOS target (default 1)"`
	Firstcore   int                `short:"f" long:"firstcore" default:"0" description:"index of first core for service thread (default 0)"`
	Group       string             `short:"g" long:"group" description:"Server group name"`
	Attach      *string            `short:"a" long:"attach_info" description:"Attach info patch (to support non-PMIx client, default /tmp)"`
	Map         *string            `short:"y" long:"map" description:"[Temporary] System map file"`
	Rank        *rank              `short:"r" long:"rank" description:"[Temporary] Self rank"`
	SocketDir   string             `short:"d" long:"socket_dir" description:"Location for all daos_server & daos_io_server sockets"`
	ShowStorage ShowStorageCommand `command:"show-storage" alias:"ss" description:"List attached SCM and NVMe storage"`
	PrepNvme    PrepNvmeCommand    `command:"prep-nvme" alias:"pn" description:"Prep NVMe devices for use with SPDK as current user"`
}

func main() {
	var err error
	defer func() {
		status := 0
		if err != nil {
			status = 1
		}
		os.Exit(status)
	}()

	runtime.GOMAXPROCS(1)

	// Set default global logger for application.
	log.NewDefaultLogger(log.Debug, "", os.Stderr)

	opts := new(cliOptions)
	p := flags.NewParser(opts, flags.Default)
	// Continue with main if no subcommand is executed.
	p.SubcommandsOptional = true

	// Parse commandline flags which override options loaded from config.
	_, err = p.Parse()
	if err != nil {
		return
	}

	// Parse configuration file and load values.
	config, err := loadConfigOpts(opts)
	if err != nil {
		log.Errorf("Failed to load config options: %s", err)
		return
	}

	// Set log level mask for default logger from config.
	switch config.ControlLogMask {
	case cLogDebug:
		log.Debugf("Switching control log level to DEBUG")
		log.SetLevel(log.Debug)
	case cLogError:
		log.Debugf("Switching control log level to ERROR")
		log.SetLevel(log.Error)
	}

	// Set log file for default logger if specified in config.
	if config.ControlLogFile != "" {
		f, err := common.AppendFile(config.ControlLogFile)
		if err != nil {
			log.Errorf("Failure creating log file: %s", err)
			return
		}
		defer f.Close()

		log.Debugf(
			"%s logging to file %s",
			os.Args[0], config.ControlLogFile)

		log.SetOutput(f)
	}

	// Backup active config.
	saveActiveConfig(&config)

	mgmtControlServer, err := newControlService(
		&config, getDrpcClientConnection(config.SocketDir))
	if err != nil {
		log.Errorf("Failed to init ControlService: %s", err)
		return
	}
	mgmtControlServer.Setup()
	defer mgmtControlServer.Teardown()

	// Create a new server register our service and listen for connections.
	addr := fmt.Sprintf("0.0.0.0:%d", config.Port)
	lis, err := net.Listen("tcp", addr)
	if err != nil {
		log.Errorf("Unable to listen on management interface: %s", err)
		return
	}

	// TODO: This will need to be extended to take certificate information for
	// the TLS protected channel. Currently it is an "insecure" channel.
	var sOpts []grpc.ServerOption
	grpcServer := grpc.NewServer(sOpts...)

	mgmtpb.RegisterMgmtControlServer(grpcServer, mgmtControlServer)

	// Set up security-related gRPC servers
	secServer := newSecurityService(getDrpcClientConnection(config.SocketDir))
	secpb.RegisterAccessControlServer(grpcServer, secServer)

	go grpcServer.Serve(lis)
	defer grpcServer.GracefulStop()

	// Format the unformatted servers.
	if err = formatIosrvs(&config, false); err != nil {
		log.Errorf("Failed to format servers: %s", err)
		return
	}

	// Create a channel to retrieve signals.
	sigchan := make(chan os.Signal, 2)
	signal.Notify(sigchan,
		syscall.SIGTERM,
		syscall.SIGINT,
		syscall.SIGQUIT,
		syscall.SIGHUP)

	// Process configurations parameters for Nvme.
	if err = config.parseNvme(); err != nil {
		log.Errorf("NVMe config could not be processed: %s", err)
		return
	}

	// Only start single io_server for now.
	// TODO: Extend to start two io_servers per host.
	iosrv, err := newIosrv(&config, 0)
	if err != nil {
		log.Errorf("Failed to load server: %s", err)
		return
	}
	if err = drpcSetup(config.SocketDir, iosrv); err != nil {
		log.Errorf("Failed to set up dRPC: %s", err)
		return
	}
	if err = iosrv.start(); err != nil {
		log.Errorf("Failed to start server: %s", err)
		return
	}

	extraText, err := CheckReplica(lis, config.AccessPoints, iosrv.cmd)
	if err != nil {
		log.Errorf(
			"Unable to determine if management service replica: %s",
			err)
		return
	}
	log.Debugf("DAOS server listening on %s%s", addr, extraText)

	// Wait for I/O server to return.
	err = iosrv.wait()
	if err != nil {
		log.Errorf("DAOS I/O server exited with error: %s", err)
	}
}
