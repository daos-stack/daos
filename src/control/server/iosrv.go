//
// (C) Copyright 2019 Intel Corporation.
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
	"io/ioutil"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"syscall"

	"github.com/pkg/errors"
	"github.com/satori/go.uuid"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/log"
)

func formatIosrvs(config *configuration, reformat bool) error {
	// Determine if an I/O server needs to createMS or bootstrapMS.
	addr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("0.0.0.0:%d", config.Port))
	if err != nil {
		return errors.WithStack(err)
	}
	createMS, bootstrapMS, err := checkMgmtSvcReplica(addr, config.AccessPoints)
	if err != nil {
		return err
	}

	for i := range config.Servers {
		// Only the first I/O server can be an MS replica.
		if i == 0 {
			err = formatIosrv(config, i, reformat, createMS, bootstrapMS)
		} else {
			err = formatIosrv(config, i, reformat, false, false)
		}
		if err != nil {
			return err
		}
	}

	return nil
}

// formatIosrv will prepare DAOS IO servers and store relevant metadata.
//
// If superblock exists and reformat not explicitly requested, no
// formatting of storage is required.
//
// If superblock does not exist, executing thread wait until administrator
// takes action to format storage via client API over gRPC channel.
//
// If format_override has been set in config and superblock does not exist,
// continue to create a new superblock without formatting.
func formatIosrv(
	config *configuration, i int, reformat, createMS, bootstrapMS bool) error {

	srv := config.Servers[i]

	op := "format"
	if reformat {
		op = "reformat"
	}
	op += " server " + srv.ScmMount

	if _, err := os.Stat(iosrvSuperPath(srv.ScmMount)); err == nil {
		log.Debugf("server %d has already been formatted\n", i)

		if reformat {
			return errors.New(op + ": reformat not implemented yet")
		}

		return nil
	} else if !os.IsNotExist(err) {
		return errors.Wrap(err, op)
	}

	if config.FormatOverride {
		log.Debugf(
			"continuing without storage format on server %d "+
				"(format_override set in config)\n", i)
	} else {
		log.Debugf("waiting for storage format on server %d\n", i)

		// wait on format storage grpc call before creating superblock
		srv.FormatCond.Wait()
	}

	// check scm has been mounted before proceeding to write to it
	if err := config.checkMount(srv.ScmMount); err != nil {
		return errors.WithMessage(
			err,
			fmt.Sprintf(
				"server%d scm mount path (%s) not mounted",
				i, srv.ScmMount))
	}

	log.Debugf(op+" (createMS=%t bootstrapMS=%t)", createMS, bootstrapMS)

	if err := createIosrvSuper(config, i, reformat, createMS, bootstrapMS); err != nil {
		return errors.WithMessage(err, op)
	}

	return nil
}

// iosrvSuper is the per-I/O-server "superblock".
type iosrvSuper struct {
	UUID        string
	System      string
	Rank        *rank
	CreateMS    bool
	BootstrapMS bool
}

// iosrvSuperPath returns the path to the I/O server superblock file.
func iosrvSuperPath(root string) string {
	return filepath.Join(root, "superblock")
}

// createIosrvSuper creates the superblock file for config.Servers[i]. Called
// when formatting an I/O server.
func createIosrvSuper(config *configuration, i int, reformat, createMS, bootstrapMS bool) error {
	// Initialize the superblock object.
	u, err := uuid.NewV4()
	if err != nil {
		return errors.Wrap(err, "generate server UUID")
	}
	super := &iosrvSuper{
		UUID:        u.String(),
		System:      config.SystemName,
		CreateMS:    createMS,
		BootstrapMS: bootstrapMS,
	}
	if config.Servers[i].Rank != nil {
		super.Rank = new(rank)
		*super.Rank = *config.Servers[i].Rank
	}

	// Write the superblock.
	return writeIosrvSuper(iosrvSuperPath(config.Servers[i].ScmMount), super)
}

func readIosrvSuper(path string) (*iosrvSuper, error) {
	b, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, errors.WithStack(err)
	}

	var s iosrvSuper
	if err = yaml.Unmarshal(b, &s); err != nil {
		return nil, errors.Wrapf(err, "unmarshal %s", path)
	}

	return &s, nil
}

func writeIosrvSuper(path string, super *iosrvSuper) error {
	data, err := yaml.Marshal(super)
	if err != nil {
		return errors.Wrapf(err, "marshal %s", path)
	}

	return common.WriteFileAtomic(path, data, 0600)
}

// iosrv represents an I/O server.
//
// NOTE: The superblock supercedes the format-time configuration in config.
type iosrv struct {
	super   *iosrvSuper
	config  *configuration
	index   int
	cmd     *exec.Cmd
	sigchld chan os.Signal
	ready   chan *srvpb.NotifyReadyReq
	conn    *drpc.ClientConnection
}

func newIosrv(config *configuration, i int) (*iosrv, error) {
	super, err := readIosrvSuper(iosrvSuperPath(config.Servers[i].ScmMount))
	if err != nil {
		return nil, err
	}

	srv := &iosrv{
		super:   super,
		config:  config,
		index:   i,
		sigchld: make(chan os.Signal, 1),
		ready:   make(chan *srvpb.NotifyReadyReq),
		conn:    getDrpcClientConnection(config.SocketDir),
	}

	return srv, nil
}

func (srv *iosrv) start() (err error) {
	defer func() {
		err = errors.WithMessagef(err, "start server %s", srv.config.Servers[srv.index].ScmMount)
	}()

	// Process bdev config parameters and write nvme.conf to SCM to be
	// consumed by SPDK in I/O server process. Populates env & cli opts.
	if err = srv.config.parseNvme(srv.index); err != nil {
		err = errors.WithMessage(
			err, "nvme config could not be processed")
		return
	}

	if err = srv.startCmd(); err != nil {
		return
	}
	defer func() {
		if err != nil {
			srv.stopCmd()
		}
	}()

	// Wait for the notifyReady request or the SIGCHLD from the I/O server.
	// If the I/O server is ready, make a dRPC connection to it.
	var ready *srvpb.NotifyReadyReq
	select {
	case ready = <-srv.ready:
	case <-srv.sigchld:
		return errors.New("received SIGCHLD")
	}
	log.Debugf("iosrv ready: %+v", *ready)
	if err = srv.conn.Connect(); err != nil {
		return
	}
	defer func() {
		if err != nil {
			srv.conn.Close()
		}
	}()

	// TODO:
	//   1 Get the rank and send a SetRank request to the I/O server.
	//   2 If CreateMs, send a CreateMs request to the I/O server.
	//   3 Clear CreateMs and BootstrapMs in the superblock.
	//   4 Send a StartMs request to the I/O server.

	return
}

func (srv *iosrv) wait() error {
	if err := srv.cmd.Wait(); err != nil {
		return errors.Wrapf(err, "wait server %s", srv.config.Servers[srv.index].ScmMount)
	}
	srv.conn.Close()
	return nil
}

func (srv *iosrv) startCmd() error {
	// Exec io_server with generated cli opts from config context.
	srv.cmd = exec.Command("daos_io_server", srv.config.Servers[srv.index].CliOpts...)
	srv.cmd.Stdout = os.Stdout
	srv.cmd.Stderr = os.Stderr
	srv.cmd.Env = os.Environ()

	// Populate I/O server environment with values from config before starting.
	srv.config.populateEnv(srv.index, &srv.cmd.Env)

	// I/O server should get a SIGKILL if this process dies.
	srv.cmd.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGKILL,
	}

	signal.Notify(srv.sigchld, syscall.SIGCHLD)

	// Start the DAOS I/O server.
	err := srv.cmd.Start()
	if err != nil {
		return errors.WithStack(err)
	}

	return nil
}

func (srv *iosrv) stopCmd() error {
	// Ignore potential errors, as the I/O server may have already died.
	srv.cmd.Process.Kill()

	if err := srv.cmd.Wait(); err != nil {
		return errors.WithStack(err)
	}

	return nil
}
