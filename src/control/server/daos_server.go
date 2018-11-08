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
	"path/filepath"
	"runtime"
	"strconv"
	"syscall"

	"github.com/jessevdk/go-flags"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/mgmt"
	mgmtpb "github.com/daos-stack/daos/src/control/mgmt/proto"
	"github.com/daos-stack/daos/src/control/utils/handlers"
)

type cliOptions struct {
	Port       uint16  `short:"p" long:"port" description:"Port for the gRPC management interfect to listen on"`
	MountPath  string  `short:"s" long:"storage" description:"Storage path"`
	ConfigPath string  `short:"o" long:"config_path" default:"etc/daos_server.yml" description:"Server config file path"`
	Modules    *string `short:"m" long:"modules" description:"List of server modules to load"`
	Cores      uint16  `short:"c" long:"cores" default:"0" description:"number of cores to use (default all)"`
	Group      string  `short:"g" long:"group" default:"daos_server" description:"Server group name"`
	Attach     *string `short:"a" long:"attach_info" description:"Attach info patch (to support non-PMIx client, default /tmp)"`
	Map        *string `short:"y" long:"map" description:"[Temporary] System map file"`
	Rank       *uint   `short:"r" long:"rank" description:"[Temporary] Self rank"`
}

var (
	opts       cliOptions
	configOpts *Configuration
	configOut  = ".daos_server.active.yml"
)

func ioArgsFromOpts(opts cliOptions) []string {

	cores := strconv.FormatUint(uint64(opts.Cores), 10)

	ioArgStr := []string{"-c", cores, "-g", opts.Group, "-s", opts.MountPath}

	if opts.Modules != nil {
		ioArgStr = append(ioArgStr, "-m", *opts.Modules)
	}
	if opts.Attach != nil {
		ioArgStr = append(ioArgStr, "-a", *opts.Attach)
	}
	if opts.Map != nil {
		ioArgStr = append(ioArgStr, "-y", *opts.Map)
	}
	if opts.Rank != nil {
		ioArgStr = append(ioArgStr, "-r", strconv.FormatUint(uint64(*opts.Rank), 10))
	}
	return ioArgStr
}

func main() {
	runtime.GOMAXPROCS(1)

	// Parse commandline flags which override options read from config
	_, err := flags.Parse(&opts)
	if err != nil {
		log.Fatalf("%s", err)
	}

	// Parse configuration file, look up absolute path if relative
	if !filepath.IsAbs(opts.ConfigPath) {
		opts.ConfigPath, err = handlers.GetAbsInstallPath(opts.ConfigPath)
		if err != nil {
			log.Fatalf("%s", err)
		}
	}
	configOpts, err = loadConfig(opts.ConfigPath)
	if err != nil {
		log.Fatalf("Configuration could not be read (%s)", err.Error())
	}
	log.Printf("DAOS config read from %s", opts.ConfigPath)

	// Populate options that can be provided on both the commandline and config
	if opts.Port == 0 {
		opts.Port = uint16(configOpts.Port)
		if opts.Port == 0 {
			log.Fatalf(
				"Unable to determine management port from config or cli opts")
		}
	}
	if opts.MountPath == "" {
		opts.MountPath = configOpts.MountPath
		if opts.MountPath == "" {
			log.Fatalf(
				"Unable to determine storage mount path from config or cli opts")
		}
	}
	// Sync opts
	configOpts.Port = int(opts.Port)
	configOpts.MountPath = opts.MountPath

	// Save read-only active configuration, try config dir then /tmp/
	activeConfig := filepath.Join(filepath.Dir(opts.ConfigPath), configOut)
	if err = saveConfig(*configOpts, activeConfig); err != nil {
		log.Printf("Active config could not be saved (%s)", err.Error())

		activeConfig = filepath.Join("/tmp", configOut)
		if err = saveConfig(*configOpts, activeConfig); err != nil {
			log.Printf("Active config could not be saved (%s)", err.Error())
		}
	}
	if err == nil {
		log.Printf("Active config saved to %s (read-only)", activeConfig)
	}

	// Create a new server register our service and listen for connections.
	addr := fmt.Sprintf("0.0.0.0:%d", opts.Port)

	log.Printf("Management interface listening on: %s", addr)

	lis, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("Unable to listen on management interface: %s", err)
	}

	// TODO: This will need to be extended to take certificat information for
	// the TLS protected channel. Currently it is an "insecure" channel.
	var sOpts []grpc.ServerOption
	grpcServer := grpc.NewServer(sOpts...)

	mgmtControlServer := mgmt.NewControlServer()
	mgmtpb.RegisterMgmtControlServer(grpcServer, mgmtControlServer)
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

	// todo: is this the right place for cleanup?
	log.Printf("Running storage teardown...")
	mgmtControlServer.Storage.Teardown()

	if err != nil {
		log.Fatal("DAOS I/O server exited with error: ", err)
	}
}
