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
	// shared memory segment ID to enable multiple SPDK instances to
	// access the same NVMe controller.
	shmID = 1
)

// SpdkSetup is an interface to configure spdk prerequisites via a
// shell script
type SpdkSetup interface {
	start() error
	reset() error
}

// spdkSetup is an implementation of the SpdkSetup interface
type spdkSetup struct{}

// CtrlrMap is a type alias for protobuf namespace messages
type CtrlrMap map[int32]*pb.NvmeController

// nvmeStorage gives access to underlying SPDK interfaces
// for accessing Nvme devices (API) as well as storing device
// details.
type nvmeStorage struct {
	logger      *log.Logger
	env         spdk.ENV  // SPDK ENV interface
	nvme        spdk.NVME // SPDK NVMe interface
	setup       SpdkSetup // SPDK shell configuration interface
	Controllers CtrlrMap
	initialised bool
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

// Init method implementation for nvmeStorage.
//
// Setup available NVMe devices to be used by SPDK
// and initialise SPDK environment before probing controllers.
//
// 	Note: shmID specified to enable SPDK multiprocess
//        mode between Discover and Burn-in.
func (n *nvmeStorage) Init() (err error) {
	if err = n.setup.start(); err != nil {
		return
	}
	// specify shmID to be set as opt in SPDK env init
	err = n.env.InitSPDKEnv(shmID)
	return
}

// Discover method implementation for nvmeStorage.
//
// Retrieves controllers and namespaces through external interface
// and populates protobuf representations in struct.
func (n *nvmeStorage) Discover() error {
	cs, ns, err := n.nvme.Discover()
	if err != nil {
		return err
	}
	return n.populate(cs, ns)
}

// Update method implementation for nvmeStorage
func (n *nvmeStorage) Update(ctrlrID int32, path string, slot int32) error {
	cs, ns, err := n.nvme.Update(ctrlrID, path, slot)
	if err != nil {
		return err
	}
	return n.populate(cs, ns)
}

// BurnIn method implementation for nvmeStorage
// Doesn't call through go-spdk, returns cmds to be issued over shell
func (n *nvmeStorage) BurnIn(pciAddr string, nsID int32, configPath string) (
	fioPath string, cmds []string, env string, err error) {
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

// Teardown method implementation for nvmeStorage.
//
// Cleanup references to NVMe devices held by go-spdk
// bindings, rebind PCI devices back to their original drivers
// and cleanup any leftover spdk files/resources.
func (n *nvmeStorage) Teardown() (err error) {
	n.nvme.Cleanup()
	err = n.setup.reset()
	return
}

// loadControllers converts slice of Controller into protobuf equivalent.
// Implemented as a pure function.
func loadControllers(
	ctrlrs []spdk.Controller, nss []spdk.Namespace) (CtrlrMap, error) {

	pbCtrlrs := make(CtrlrMap)
	for _, c := range ctrlrs {
		pbCtrlrs[c.ID] = &pb.NvmeController{
			Id:      c.ID,
			Model:   c.Model,
			Serial:  c.Serial,
			Pciaddr: c.PCIAddr,
			Fwrev:   c.FWRev,
			// repeated pb field
			Namespace: loadNamespaces(c.ID, nss),
		}
	}
	if len(pbCtrlrs) != len(ctrlrs) {
		return nil, fmt.Errorf("loadControllers: input contained duplicate keys")
	}
	return pbCtrlrs, nil
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

// populate unpacks return type and loads protobuf representations.
func (n *nvmeStorage) populate(inCtrlrs []spdk.Controller, inNss []spdk.Namespace) error {
	ctrlrs, err := loadControllers(inCtrlrs, inNss)
	if err != nil {
		return err
	}
	n.Controllers = ctrlrs
	n.initialised = true
	return nil
}

// newNvmeStorage creates a new instance of nvmeStorage struct.
func newNvmeStorage(logger *log.Logger) *nvmeStorage {
	return &nvmeStorage{
		logger: logger,
		env:    &spdk.Env{},
		nvme:   &spdk.Nvme{},
		setup:  &spdkSetup{},
	}
}
