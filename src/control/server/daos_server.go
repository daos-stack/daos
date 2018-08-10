//
// (C) Copyright 2018 Intel Corporation.
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
	"strconv"
	"syscall"

	"github.com/jessevdk/go-flags"
	"google.golang.org/grpc"

	"modules/mgmt"
	mgmtpb "modules/mgmt/proto"
	"modules/security"
	secpb "modules/security/proto"
)

type daosOptions struct {
	Port    uint16  `short:"p" long:"port" default:"10000" description:"Port for the gRPC management interfect to listen on"`
	Modules *string `short:"m" long:"modules" description:"List of server modules to load"`
	Cores   uint16  `short:"c" long:"cores" default:"0" description:"number of cores to use (default all)"`
	Group   string  `short:"g" long:"group" default:"daos_server" description:"Server group name"`
	Storage string  `short:"s" long:"storage" default:"/mnt/daos" description:"Storage path"`
	Attach  *string `short:"a" long:"attach_info" description:"Attach info patch (to support non-PMIx client, default /tmp)"`
}

var opts daosOptions

func ioArgsFromOpts(opts daosOptions) []string {

	cores := strconv.FormatUint(uint64(opts.Cores), 10)

	ioArgStr := []string{"-c", cores, "-g", opts.Group, "-s", opts.Storage}

	if opts.Modules != nil {
		ioArgStr = append(ioArgStr, "-m", *opts.Modules)
	}
	if opts.Attach != nil {
		ioArgStr = append(ioArgStr, "-a", *opts.Attach)
	}
	return ioArgStr
}

func main() {
	runtime.GOMAXPROCS(1)

	_, err := flags.Parse(&opts)
	if err != nil {
		log.Fatalf("%s", err)
	}

	addr := fmt.Sprintf("0.0.0.0:%d", opts.Port)

	log.Printf("Management interface listening on: %s", addr)

	lis, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("Unable to listen on management interface: %s", err)
	}

	// Create a new server register our service and listen for connections.
	// TODO: This will need to be extended to take certificat information for
	// the TLS protected channel. Currently it is an "insecure" channel.
	var sOpts []grpc.ServerOption
	grpcServer := grpc.NewServer(sOpts...)
	secpb.RegisterSecurityControlServer(grpcServer, security.NewControlServer())
	mgmtpb.RegisterMgmtControlServer(grpcServer, mgmt.NewControlServer())
	go grpcServer.Serve(lis)
	defer grpcServer.GracefulStop()

	// create a channel to retrieve signals
	sigchan := make(chan os.Signal, 2)
	signal.Notify(sigchan,
		syscall.SIGTERM,
		syscall.SIGINT,
		syscall.SIGQUIT,
		syscall.SIGKILL,
		syscall.SIGHUP)

	// setup cmd line to start the DAOS I/O server
	ioArgs := ioArgsFromOpts(opts)
	srv := exec.Command("daos_io_server", ioArgs...)
	srv.Stdout = os.Stdout
	srv.Stderr = os.Stderr

	// I/O server should get a SIGTERM if this process dies
	srv.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGTERM,
	}

	// start the DAOS I/O server
	err = srv.Start()
	if err != nil {
		log.Fatal("DAOS I/O server failed to start: ", err)
	}

	// catch signals
	go func() {
		<-sigchan
		if err := srv.Process.Kill(); err != nil {
			log.Fatal("Failed to kill DAOS I/O server: ", err)
		}
	}()

	// wait for I/O server to return
	err = srv.Wait()
	if err != nil {
		log.Fatal("DAOS I/O server exited with error: ", err)
	}
}
