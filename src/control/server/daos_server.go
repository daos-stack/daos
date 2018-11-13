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
	"strings"
	"syscall"

	"github.com/daos-stack/daos/src/control/drpc"

	flags "github.com/jessevdk/go-flags"
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
	SocketDir  string  `short:"d" long:"socket_dir" description:"Location for all daos_server & daos_io_server sockets"`
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
	if opts.SocketDir == "" {
		opts.SocketDir = configOpts.SocketDir
		if opts.SocketDir == "" {
			log.Fatalf(
				"Unable to determine socket directory from config or cli opts")
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
	lis, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("Unable to listen on management interface: %s", err)
	}

	// TODO: This will need to be extended to take certificat information for
	// the TLS protected channel. Currently it is an "insecure" channel.
	var sOpts []grpc.ServerOption
	grpcServer := grpc.NewServer(sOpts...)

	mgmtControlServer := mgmt.NewControlServer()
	defer mgmtControlServer.Teardown()
	mgmtpb.RegisterMgmtControlServer(grpcServer, mgmtControlServer)
	go grpcServer.Serve(lis)
	defer grpcServer.GracefulStop()

	// Create our socket directory if it doesn't exist
	_, err = os.Stat(opts.SocketDir)
	if err != nil && os.IsPermission(err) {
		log.Fatalf("User does not have permission to access %s", opts.SocketDir)
	} else if err != nil && os.IsNotExist(err) {
		err = os.MkdirAll(opts.SocketDir, 0755)
		if err != nil {
			log.Printf("%s", err)
			log.Fatalf("Unable to create socket directory: %s", opts.SocketDir)
		}
	}

	sockPath := filepath.Join(opts.SocketDir, "daos_server.sock")
	drpcServer, err := drpc.NewDomainSocketServer(sockPath)
	if err != nil {
		log.Fatalf("Unable to create socket server: %v", err)
	}

	err = drpcServer.Start()
	if err != nil {
		log.Fatalf("Unable to start socket server on %s: %v", sockPath, err)
	}

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

	// If this server is supposed to host an MS replica, format and start
	// the MS replica. Only performing the check and print the result for now.
	isReplica, bootstrap, err := checkMgmtSvcReplica(lis.Addr().(*net.TCPAddr), configOpts.AccessPoints)
	if err != nil {
		srv.Process.Kill()
		log.Fatal("Failed to check management service replica: ", err)
	}
	var msReplicaCheck string
	if isReplica {
		msReplicaCheck = " as access point"
		if bootstrap {
			msReplicaCheck += " (bootstrap)"
		}
	}

	log.Printf("DAOS server listening on %s%s", addr, msReplicaCheck)

	// wait for I/O server to return
	err = srv.Wait()
	if err != nil {
		log.Fatal("DAOS I/O server exited with error: ", err)
	}
}

// getInterfaceAddrs enables TestCheckMgmtSvcReplica to replace the real
// interface query with a sample data set.
var getInterfaceAddrs = func() ([]net.Addr, error) {
	return net.InterfaceAddrs()
}

// checkMgmtSvcReplica determines if this server is supposed to host an MS
// replica, based on this server's management address and the system access
// points. If bootstrap is true, in which case isReplica must be true, this
// replica shall bootstrap the MS.
func checkMgmtSvcReplica(self *net.TCPAddr, accessPoints []string) (isReplica, bootstrap bool, err error) {
	replicas, err := resolveAccessPoints(accessPoints)
	if err != nil {
		return false, false, err
	}

	selves, err := getListenIPs(self)
	if err != nil {
		return false, false, err
	}

	// Check each replica against this server's listen IPs.
	for i := range replicas {
		if replicas[i].Port != self.Port {
			continue
		}
		for _, ip := range selves {
			if ip.Equal(replicas[i].IP) {
				// The first replica in the access point list
				// shall bootstrap the MS.
				if i == 0 {
					return true, true, nil
				}
				return true, false, nil
			}
		}
	}

	return false, false, nil
}

// resolveAccessPoints resolves the strings in accessPoints into addresses in
// addrs. If a port isn't specified, assume the default port.
func resolveAccessPoints(accessPoints []string) (addrs []*net.TCPAddr, err error) {
	defaultPort := NewDefaultConfiguration().Port
	for _, ap := range accessPoints {
		if !hasPort(ap) {
			ap = net.JoinHostPort(ap, strconv.Itoa(defaultPort))
		}
		t, err := net.ResolveTCPAddr("tcp", ap)
		if err != nil {
			return nil, err
		}
		addrs = append(addrs, t)
	}
	return addrs, nil
}

// hasPort checks if addr specifies a port. This only works with IPv4
// addresses at the moment.
func hasPort(addr string) bool {
	return strings.Contains(addr, ":")
}

// getListenIPs takes the address this server listens on and returns a list of
// the corresponding IPs.
func getListenIPs(listenAddr *net.TCPAddr) (listenIPs []net.IP, err error) {
	if listenAddr.IP.IsUnspecified() {
		// Find the IPs of all IP interfaces.
		addrs, err := getInterfaceAddrs()
		if err != nil {
			return nil, err
		}
		for _, a := range addrs {
			// Ignore non-IP interfaces.
			in, ok := a.(*net.IPNet)
			if ok {
				listenIPs = append(listenIPs, in.IP)
			}
		}
	} else {
		listenIPs = append(listenIPs, listenAddr.IP)
	}
	return listenIPs, nil
}
