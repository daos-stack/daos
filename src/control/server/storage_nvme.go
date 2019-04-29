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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"

	"github.com/daos-stack/go-spdk/spdk"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

const (
	spdkSetupPath      = "share/control/setup_spdk.sh"
	spdkFioPluginDir   = "share/spdk/fio_plugin"
	fioExecPath        = "bin/fio"
	defaultNrHugepages = 1024
	nrHugepagesEnv     = "_NRHUGE"
	targetUserEnv      = "_TARGET_USER"
	pciWhiteListEnv    = "_PCI_WHITELIST"
)

// SpdkSetup is an interface to configure spdk prerequisites via a
// shell script
type SpdkSetup interface {
	prep(int, string, string) error
	reset() error
}

// spdkSetup is an implementation of the SpdkSetup interface
type spdkSetup struct {
	scriptPath  string
	nrHugePages int
}

// NvmeStorage interface specifies basic functionality for subsystem
type NvmeStorage interface {
	Setup() error
	Teardown() error
	Format(int) error
	Discover() error
}

// nvmeStorage gives access to underlying SPDK interfaces
// for accessing Nvme devices (API) as well as storing device
// details.
type nvmeStorage struct {
	env         spdk.ENV       // SPDK ENV interface
	nvme        spdk.NVME      // SPDK NVMe interface
	spdk        SpdkSetup      // SPDK shell configuration interface
	config      *configuration // server configuration structure
	controllers []*pb.NvmeController
	initialized bool
	formatted   bool
}

// prep executes setup script to allocate hugepages and unbind PCI devices
// (that don't have active mountpoints) from generic kernel driver to be
// used with SPDK. Either all PCI devices will be unbound by default if wlist
// parameter is not set, otherwise PCI devices can be specified by passing in a
// whitelist of PCI addresses.
//
// NOTE: will make the controller disappear from /dev until reset() called.
func (s *spdkSetup) prep(nrHugepages int, usr string, wlist string) error {
	srv := exec.Command(s.scriptPath)
	srv.Env = os.Environ()
	var stderr bytes.Buffer
	srv.Stderr = &stderr
	var hPages, tUsr, whitelist string

	if nrHugepages <= 0 {
		nrHugepages = defaultNrHugepages
	}
	hPages = nrHugepagesEnv + "=" + strconv.Itoa(nrHugepages)
	srv.Env = append(srv.Env, hPages)
	log.Debugf("spdk setup with %s\n", hPages)

	tUsr = targetUserEnv + "=" + usr
	srv.Env = append(srv.Env, tUsr)
	log.Debugf("spdk setup with %s\n", tUsr)

	if wlist != "" {
		whitelist = pciWhiteListEnv + "=" + wlist
		srv.Env = append(srv.Env, whitelist)
		log.Debugf("spdk setup with %s\n", whitelist)
	}

	return errors.Wrapf(
		srv.Run(),
		"spdk setup failed (%s, %s, %s, %s)",
		hPages, tUsr, whitelist, stderr.String())
}

// reset executes setup script to deallocate hugepages & return PCI devices
// to previous driver bindings.
//
// NOTE: will make the controller reappear in /dev.
func (s *spdkSetup) reset() error {
	srv := exec.Command(s.scriptPath, "reset")
	var stderr bytes.Buffer
	srv.Stderr = &stderr

	return errors.Wrapf(
		srv.Run(),
		"spdk reset failed (%s)",
		stderr.String())
}

// Setup method implementation for nvmeStorage.
//
// Perform any setup to be performed before accessing NVMe devices.
// NOTE: doesn't attempt SPDK prep which requires elevated privileges,
//       that instead can be performed explicitly with subcommand.
func (n *nvmeStorage) Setup() (err error) {
	if err = n.Discover(); err != nil {
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
	// TODO: Decide whether to rebind PCI devices back to their original
	// drivers and release hugepages here.
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
	if err := n.env.InitSPDKEnv(n.config.NvmeShmID); err != nil {
		return errors.WithMessage(err, "SPDK env init, has setup been run?")
	}
	cs, ns, err := n.nvme.Discover()
	if err != nil {
		return errors.WithMessage(err, "SPDK discovery")
	}
	n.controllers = loadControllers(cs, ns)
	n.initialized = true
	return nil
}

// Format attempts to format (forcefully) NVMe devices on a given server
// as specified in config file.
func (n *nvmeStorage) Format(idx int) error {
	if !n.initialized {
		return errors.New("nvme storage not initialized")
	}
	if n.formatted {
		return errors.New(
			"nvme storage has already been formatted and reformat " +
				"not implemented")
	}

	srv := n.config.Servers[idx]

	switch srv.BdevClass {
	case bdNVMe:
		for _, pciAddr := range srv.BdevList {
			if pciAddr == "" {
				return errors.New("bdev nvme device list entry empty")
			}

			cs, ns, err := n.nvme.Format(pciAddr)
			if err != nil {
				return errors.Wrap(err, "nvme format")
			}
			n.controllers = loadControllers(cs, ns)
		}
	default:
		return errors.Errorf(
			"format unsupported on BdevClass %v", srv.BdevClass)
	}

	n.formatted = true
	return nil
}

// Update method implementation for nvmeStorage
func (n *nvmeStorage) Update(pciAddr string, path string, slot int32) error {
	if !n.initialized {
		return errors.New("nvme storage not initialized")
	}

	cs, ns, err := n.nvme.Update(pciAddr, path, slot)
	if err != nil {
		return err
	}

	n.controllers = loadControllers(cs, ns)
	return nil
}

// BurnIn method implementation for nvmeStorage
// Doesn't call through go-spdk, returns cmds to be issued over shell
func (n *nvmeStorage) BurnIn(pciAddr string, nsID int32, configPath string) (
	fioPath string, cmds []string, env string, err error) {

	if !n.initialized {
		err = errors.New("nvme storage not initialized")
		return
	}

	pluginDir := ""
	pluginDir, err = common.GetAbsInstallPath(spdkFioPluginDir)
	if err != nil {
		return
	}

	fioPath, err = common.GetAbsInstallPath(fioExecPath)
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
	log.Debugf(
		"BurnIn command string: %s %s %v", env, fioPath, cmds)

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
				Model:   c.Model,
				Serial:  c.Serial,
				Pciaddr: c.PCIAddr,
				Fwrev:   c.FWRev,
				// repeated pb field
				Namespace: loadNamespaces(c.PCIAddr, nss),
			})
	}
	return pbCtrlrs
}

// loadNamespaces converts slice of Namespace into protobuf equivalent.
// Implemented as a pure function.
func loadNamespaces(
	ctrlrPciAddr string, nss []spdk.Namespace) (_nss []*pb.NvmeNamespace) {

	for _, ns := range nss {
		if ns.CtrlrPciAddr == ctrlrPciAddr {
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
func newNvmeStorage(config *configuration) (*nvmeStorage, error) {

	scriptPath, err := common.GetAbsInstallPath(spdkSetupPath)
	if err != nil {
		return nil, err
	}
	return &nvmeStorage{
		env:    &spdk.Env{},
		nvme:   &spdk.Nvme{},
		spdk:   &spdkSetup{scriptPath, config.NrHugepages},
		config: config,
	}, nil
}
