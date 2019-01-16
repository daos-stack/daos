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
	"log"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"runtime"
	"syscall"

	flags "github.com/jessevdk/go-flags"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/mgmt"
	mgmtpb "github.com/daos-stack/daos/src/control/mgmt/proto"
)

func main() {
	runtime.GOMAXPROCS(1)

	var opts mgmt.CliOptions

	// Parse commandline flags which override options loaded from config.
	_, err := flags.Parse(&opts)
	if err != nil {
		println(err)
		return
	}

	// Parse configuration file and load values, then backup active config.
	config := mgmt.SaveActiveConfig(mgmt.LoadConfigOpts(&opts))

	mgmtControlServer := mgmt.NewControlService(&config)
	mgmtControlServer.Setup()
	defer mgmtControlServer.Teardown()

	// If command mode option specified then perform task and exit.
	if opts.ShowStorage {
		mgmtControlServer.ShowLocalStorage()
		return
	}

	// Create a new server register our service and listen for connections.
	addr := fmt.Sprintf("0.0.0.0:%d", config.Port)
	lis, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatal("Unable to listen on management interface: ", err)
	}

	// TODO: This will need to be extended to take certificate information for
	// the TLS protected channel. Currently it is an "insecure" channel.
	var sOpts []grpc.ServerOption
	grpcServer := grpc.NewServer(sOpts...)

	mgmtpb.RegisterMgmtControlServer(grpcServer, mgmtControlServer)
	go grpcServer.Serve(lis)
	defer grpcServer.GracefulStop()

	// Init socket and start drpc server to communicate with DAOS I/O servers.
	drpcSetup(config.SocketDir)

	// Create a channel to retrieve signals.
	sigchan := make(chan os.Signal, 2)
	signal.Notify(sigchan,
		syscall.SIGTERM,
		syscall.SIGINT,
		syscall.SIGQUIT,
		syscall.SIGKILL,
		syscall.SIGHUP)

	// Process configurations parameters for Nvme.
	if err = config.ParseNvme(); err != nil {
		log.Fatal("NVMe config could not be processed: ", err)
	}

	// Only start single io_server for now.
	// TODO: Extend to start two io_servers per host.
	ioIdx := 0

	// Exec io_server with generated cli opts from config context.
	srv := exec.Command("daos_io_server", config.Servers[ioIdx].CliOpts...)
	srv.Stdout = os.Stdout
	srv.Stderr = os.Stderr
	srv.Env = os.Environ()

	// Populate I/O server environment with values from config before starting.
	if err = config.PopulateEnv(ioIdx, &srv.Env); err != nil {
		log.Fatal("DAOS I/O env vars could not be populated: ", err)
	}

	// I/O server should get a SIGTERM if this process dies.
	srv.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGTERM,
	}

	// Start the DAOS I/O server.
	err = srv.Start()
	if err != nil {
		log.Fatal("DAOS I/O server failed to start: ", err)
	}

	// Catch signals raised by I/O server.
	go func() {
		<-sigchan
		if err := srv.Process.Kill(); err != nil {
			log.Fatal("Failed to kill DAOS I/O server: ", err)
		}
	}()

	log.Printf(
		"DAOS server listening on %s%s", addr,
		mgmt.CheckReplica(lis, config.AccessPoints, srv))

	// Wait for I/O server to return.
	err = srv.Wait()
	if err != nil {
		log.Fatal("DAOS I/O server exited with error: ", err)
	}
}
