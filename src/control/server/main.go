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
	"os/user"
	"strconv"
	"syscall"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	secpb "github.com/daos-stack/daos/src/control/security/proto"
	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	//#include <unistd.h>
	//#include <errno.h>
	"C"
)

func main() {
	if serverMain() != nil {
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

// drop will attempt to drop privileges by setting uid of running process to
// that of the username specified in config file. If groupname is also
// specified in config file then check user is a member of that group and
// set relevant gid if so, otherwise use user.gid.
func drop(userName string, groupName string) error {
	log.Debugf("Running as root, downgrading to user ", userName)

	if userName == "" {
		return errors.New("no username supplied in config")
	}

	usr, err := user.Lookup(userName)
	if err != nil {
		return errors.WithMessage(err, "user lookup")
	}

	uid, err := strconv.ParseInt(usr.Uid, 10, 32)
	if err != nil {
		return errors.WithMessage(err, "parsing uid to int")
	}

	_gid := usr.Gid

	// attempt to assign group specified in config file
	if group, err := user.LookupGroup(groupName); err == nil {
		// check user group membership
		if ids, err := usr.GroupIds(); err == nil {
			for _, g := range ids {
				if group.Gid == g {
					_gid = g
					break
				}
			}

			if _gid != group.Gid {
				log.Debugf(
					"user %s not member of group %s",
					usr.Username, group.Name)
			}
		} else {
			return errors.WithMessage(err, "get group membership")
		}
	} else {
		log.Debugf("lookup of group specified in config: %+v", err)
	}

	gid, err := strconv.ParseInt(_gid, 10, 32)
	if err != nil {
		return errors.WithMessage(err, "parsing gid to int")
	}

	cerr, errno := C.setgid(C.__gid_t(gid))
	if cerr != 0 {
		return errors.WithMessagef(
			err, "setting gid (C.setgid) (%d)", errno)
	}

	cerr, errno = C.setuid(C.__uid_t(uid))
	if cerr != 0 {
		return errors.WithMessagef(
			err, "setting uid (C.setuid) (%d)", errno)
	}

	return nil
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
	mgmtControlServer, err := newControlService(
		&config, getDrpcClientConnection(config.SocketDir))
	if err != nil {
		log.Errorf("Failed to init ControlService: %s", err)
		return err
	}
	mgmtControlServer.Setup()
	defer mgmtControlServer.Teardown()

	// Create listener on management network.
	addr := fmt.Sprintf("0.0.0.0:%d", config.Port)
	lis, err := net.Listen("tcp", addr)
	if err != nil {
		log.Errorf("Unable to listen on management interface: %s", err)
		return err
	}
	log.Debugf("DAOS control server listening on %s", addr)

	// Create new grpc server, register services and start serving.
	var sOpts []grpc.ServerOption
	// TODO: This will need to be extended to take certificate information for
	// the TLS protected channel. Currently it is an "insecure" channel.
	grpcServer := grpc.NewServer(sOpts...)

	mgmtpb.RegisterMgmtControlServer(grpcServer, mgmtControlServer)
	secServer := newSecurityService(getDrpcClientConnection(config.SocketDir))
	secpb.RegisterAccessControlServer(grpcServer, secServer)

	go grpcServer.Serve(lis)
	defer grpcServer.GracefulStop()

	// Format the unformatted servers and related hardware.
	if err = formatIosrvs(&config, false); err != nil {
		log.Errorf("Failed to format servers: %s", err)
		return err
	}

	if syscall.Getuid() == 0 {
		log.Debugf("Dropping privileges...")
		if err := drop(config.UserName, config.GroupName); err != nil {
			log.Errorf("Failed to drop privileges: %s", err)
			// TODO: don't continue as root
		}
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

	return err
}
