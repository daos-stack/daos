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
	"path/filepath"
	"syscall"

	"github.com/pkg/errors"
	"github.com/satori/go.uuid"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
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

func formatIosrv(config *configuration, i int, reformat, createMS, bootstrapMS bool) error {
	op := "format"
	if reformat {
		op = "reformat"
	}
	op += " server " + config.Servers[i].ScmMount

	if _, err := os.Stat(iosrvSuperPath(config.Servers[i].ScmMount)); err == nil {
		if reformat {
			return errors.New(op + ": reformat not implemented yet")
		}
		return nil
	} else if !os.IsNotExist(err) {
		return errors.Wrap(err, op)
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
	super  *iosrvSuper
	config *configuration
	index  int
	cmd    *exec.Cmd
}

func newIosrv(config *configuration, i int) (*iosrv, error) {
	super, err := readIosrvSuper(iosrvSuperPath(config.Servers[i].ScmMount))
	if err != nil {
		return nil, err
	}

	srv := &iosrv{
		super:  super,
		config: config,
		index:  i,
	}

	return srv, nil
}

func (srv *iosrv) start() (err error) {
	defer func() { err = errors.WithMessagef(err, "start server %s", srv.config.Servers[srv.index].ScmMount) }()

	if err = srv.startCmd(); err != nil {
		return
	}
	defer func() {
		if err != nil {
			srv.stopCmd()
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
