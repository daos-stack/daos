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
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/utils/handlers"
	"github.com/daos-stack/daos/src/control/utils/log"

	"github.com/daos-stack/go-spdk/spdk"

	pb "github.com/daos-stack/daos/src/control/proto/mgmt"
)

var (
	spdkSetupPath    = "share/control/setup_spdk.sh"
	spdkFioPluginDir = "share/spdk/fio_plugin"
	fioExecPath      = "bin/fio"
	nrHugepagesEnv   = "_NRHUGE"
	targetUserEnv    = "_TARGET_USER"
)

// SpdkSetup is an interface to configure spdk prerequisites via a
// shell script
type SpdkSetup interface {
	start() error
	reset() error
}

// spdkSetup is an implementation of the SpdkSetup interface
type spdkSetup struct {
	scriptPath  string
	nrHugePages int
}

// nvmeStorage gives access to underlying SPDK interfaces
// for accessing Nvme devices (API) as well as storing device
// details.
type nvmeStorage struct {
	logger      *log.Logger
	env         spdk.ENV  // SPDK ENV interface
	nvme        spdk.NVME // SPDK NVMe interface
	setup       SpdkSetup // SPDK shell configuration interface
	shmID       int       // SPDK init opts param to enable multi-process mode
	Controllers []*pb.NvmeController
	initialized bool
}

// start executes setup script to allocate hugepages and bind PCI devices
// (that don't have active mountpoints) to generic kernel driver.
//
// NOTE: will make the controller disappear from /dev until reset() called.
func (s *spdkSetup) start() error {
	if err := s.reset(); err != nil {
		return err
	}

	srv := exec.Command(s.scriptPath)
	srv.Env = os.Environ()
	var stderr bytes.Buffer
	srv.Stderr = &stderr
	var hp, tUsr string

	if s.nrHugePages != 0 {
		hp = nrHugepagesEnv + "=" + strconv.Itoa(s.nrHugePages)
		srv.Env = append(srv.Env, hp)
	}
	usr := os.Getenv("USER")
	if usr == "" {
		return errors.New("missing USER envar")
	}
	tUsr = targetUserEnv + "=" + usr
	srv.Env = append(srv.Env, tUsr)

	return errors.Wrapf(
		srv.Run(),
		"spdk setup failed (%s, %s, %s), is no-password sudo enabled?",
		hp, tUsr, stderr.String())
}

// reset executes setup script to deallocate hugepages & return PCI devices
// to previous driver bindings.
//
// NOTE: will make the controller reappear in /dev.
func (s *spdkSetup) reset() (err error) {
	srv := exec.Command(s.scriptPath, "reset")
	var stderr bytes.Buffer
	srv.Stderr = &stderr
	return errors.Wrapf(
		srv.Run(),
		"spdk reset failed (%s), is no-password sudo enabled?",
		stderr.String())
}

// Setup method implementation for nvmeStorage.
//
// Perform any setup to be performed before accessing NVMe devices.
func (n *nvmeStorage) Setup() (err error) {
	if err = n.setup.start(); err != nil {
		return
	}
	return
}

// Teardown method implementation for nvmeStorage.
//
// Perform any teardown to be performed after accessing NVMe devices.
func (n *nvmeStorage) Teardown() (err error) {
	// Cleanup references to NVMe devices held by go-spdk bindings
	n.nvme.Cleanup()
	// Rebind PCI devices back to their original drivers and cleanup any
	// leftover spdk files/resources.
	// err = n.setup.reset()
	n.initialized = false
	return
}

// Discover method implementation for nvmeStorage.
//
// Initialise SPDK environment before probing controllers then retrieve
// controller and namespace details through external interface and populate
// protobuf representations.
//
// Init NVMe subsystem with shm_id in controlService (PRIMARY SPDK process) and
// later share with io_server (SECONDARY SPDK process) to facilitate concurrent
// SPDK access to controllers on same host from multiple processes.
//
// TODO: This is currently a one-time only discovery for the lifetime of this
//       process, presumably we want to be able to detect updates during
//       process lifetime.
func (n *nvmeStorage) Discover() error {
	if n.initialized {
		return nil
	}
	// specify shmID to be set as opt in SPDK env init
	if err := n.env.InitSPDKEnv(n.shmID); err != nil {
		return err
	}
	cs, ns, err := n.nvme.Discover()
	if err != nil {
		return err
	}
	n.Controllers = loadControllers(cs, ns)
	n.initialized = true
	return nil
}

// Update method implementation for nvmeStorage
func (n *nvmeStorage) Update(ctrlrID int32, path string, slot int32) error {
	if n.initialized {
		cs, ns, err := n.nvme.Update(ctrlrID, path, slot)
		if err != nil {
			return err
		}
		n.Controllers = loadControllers(cs, ns)
		return nil
	}
	return fmt.Errorf("nvme storage not initialized")
}

// BurnIn method implementation for nvmeStorage
// Doesn't call through go-spdk, returns cmds to be issued over shell
func (n *nvmeStorage) BurnIn(pciAddr string, nsID int32, configPath string) (
	fioPath string, cmds []string, env string, err error) {
	if n.initialized {
		pluginDir := ""
		pluginDir, err = handlers.GetAbsInstallPath(spdkFioPluginDir)
		if err != nil {
			return
		}
		fioPath, err = handlers.GetAbsInstallPath(fioExecPath)
		if err != nil {
			return
		}
		// run fio with spdk plugin specified in LD_PRELOAD env
		env = fmt.Sprintf("LD_PRELOAD=%s/fio_plugin", pluginDir)
		// limitation of fio_plugin for spdk is that traddr needs
		// to not contain colon chars, convert to full-stops
		// https://github.com/spdk/spdk/tree/master/examples/nvme/fio_plugin .
		// shm_id specified within fio configs to enable spdk multiprocess
		// mode required to perform burn-in from Go process.
		// eta options provided to trigger periodic client responses.
		cmds = []string{
			fmt.Sprintf(
				"--filename=\"trtype=PCIe traddr=%s ns=%d\"",
				strings.Replace(pciAddr, ":", ".", -1), nsID),
			"--ioengine=spdk",
			"--eta=always",
			"--eta-newline=10",
			configPath,
		}
		n.logger.Debugf(
			"BurnIn command string: %s %s %v", env, fioPath, cmds)
		return
	}
	err = fmt.Errorf("nvme storage not initialized")
	return
}

// loadControllers converts slice of Controller into protobuf equivalent.
// Implemented as a pure function.
func loadControllers(ctrlrs []spdk.Controller, nss []spdk.Namespace) (
	pbCtrlrs []*pb.NvmeController) {
	for _, c := range ctrlrs {
		pbCtrlrs = append(
			pbCtrlrs,
			&pb.NvmeController{
				Id:      c.ID,
				Model:   c.Model,
				Serial:  c.Serial,
				Pciaddr: c.PCIAddr,
				Fwrev:   c.FWRev,
				// repeated pb field
				Namespace: loadNamespaces(c.ID, nss),
			})
	}
	return pbCtrlrs
}

// loadNamespaces converts slice of Namespace into protobuf equivalent.
// Implemented as a pure function.
func loadNamespaces(ctrlrID int32, nss []spdk.Namespace) (_nss []*pb.NvmeNamespace) {
	for _, ns := range nss {
		if ns.CtrlrID == ctrlrID {
			_nss = append(
				_nss,
				&pb.NvmeNamespace{
					Id:       ns.ID,
					Capacity: ns.Size,
				})
		}
	}
	return
}

// newNvmeStorage creates a new instance of nvmeStorage struct.
func newNvmeStorage(
	logger *log.Logger, shmID int, nrHugePages int) (*nvmeStorage, error) {

	scriptPath, err := handlers.GetAbsInstallPath(spdkSetupPath)
	if err != nil {
		return nil, err
	}
	return &nvmeStorage{
		logger: logger,
		env:    &spdk.Env{},
		nvme:   &spdk.Nvme{},
		setup:  &spdkSetup{scriptPath, nrHugePages},
		shmID:  shmID, // required to enable SPDK multi-process mode
	}, nil
}
