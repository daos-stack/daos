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

package mgmt

import (
	"fmt"
	"os/exec"
	"strings"

	"github.com/daos-stack/daos/src/control/utils/handlers"
	"github.com/daos-stack/daos/src/control/utils/log"

	"github.com/daos-stack/go-spdk/spdk"

	pb "github.com/daos-stack/daos/src/control/mgmt/proto"
)

var (
	spdkSetupPath    = "share/spdk/scripts/setup.sh"
	spdkFioPluginDir = "share/spdk/fio_plugin"
	fioExecPath      = "bin/fio"
)

// SpdkSetup is an interface to configure spdk prerequisites via a
// shell script
type SpdkSetup interface {
	start() error
	reset() error
}

// spdkSetup is an implementation of the SpdkSetup interface
type spdkSetup struct{}

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
func (s *spdkSetup) start() (err error) {
	absSetupPath, err := handlers.GetAbsInstallPath(spdkSetupPath)
	if err != nil {
		return
	}
	err = exec.Command(absSetupPath).Run()
	return
}

// reset executes setup script to deallocate hugepages & return PCI devices
// to previous driver bindings.
//
// NOTE: will make the controller reappear in /dev.
func (s *spdkSetup) reset() (err error) {
	absSetupPath, err := handlers.GetAbsInstallPath(spdkSetupPath)
	if err != nil {
		return
	}
	err = exec.Command(absSetupPath, "reset").Run()
	return
}

// Setup method implementation for nvmeStorage.
//
// Initialise SPDK environment before probing controllers then retrieve
// controller and namespace details through external interface and populate
// protobuf representations.
//
// Note: SPDK multi-process mode can be enabled by supplying shm_id in
//       spdk_env_opts for all applications accessing SSD with SPDK. This is
//       specifically used to work around the fact that SPDK throws exceptions
//       if you try to probe a second time.
// Todo: this is currently a one-time only discovery for the lifetime of this
//       process, presumably we want to be able to detect updates during
//       process lifetime.
func (n *nvmeStorage) Setup() (err error) {
	if n.initialized {
		return fmt.Errorf("nvme storage already initialized")
	}
	if err = n.setup.start(); err != nil {
		return
	}
	// specify shmID to be set as opt in SPDK env init
	if err = n.env.InitSPDKEnv(n.shmID); err != nil {
		return
	}
	cs, ns, err := n.nvme.Discover()
	if err != nil {
		return err
	}
	n.Controllers = loadControllers(cs, ns)
	n.initialized = true
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
// Currently a placeholder verifying devices have been retrieved during Setup()
// In future may retrieve a more up-to-date view.
func (n *nvmeStorage) Discover() error {
	if n.initialized {
		return nil
	}
	return fmt.Errorf("nvme storage not initialized")
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
func newNvmeStorage(logger *log.Logger, shmID int) *nvmeStorage {
	return &nvmeStorage{
		logger: logger,
		env:    &spdk.Env{},
		nvme:   &spdk.Nvme{},
		setup:  &spdkSetup{},
		shmID:  shmID, // required to enable SPDK multi-process mode
	}
}
