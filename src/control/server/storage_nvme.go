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

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/daos-stack/go-spdk/spdk"
	"github.com/pkg/errors"
)

const (
	spdkSetupPath      = "share/daos/control/setup_spdk.sh"
	spdkFioPluginDir   = "share/daos/spdk/fio_plugin"
	fioExecPath        = "bin/fio"
	defaultNrHugepages = 1024
	nrHugepagesEnv     = "_NRHUGE"
	targetUserEnv      = "_TARGET_USER"
	pciWhiteListEnv    = "_PCI_WHITELIST"

	msgBdevAlreadyFormatted = "nvme storage has already been formatted and " +
		"reformat not implemented"
	msgBdevNotFound = "controller at pci addr not found, check device exists " +
		"and can be discovered, you may need to run `sudo daos_server " +
		"storage prep-nvme` to setup SPDK to access SSDs"
	msgBdevNotInited          = "nvme storage not initialized"
	msgBdevClassNotSupported  = "operation unsupported on bdev class"
	msgSpdkInitFail           = "SPDK env init, has setup been run?"
	msgSpdkDiscoverFail       = "SPDK controller discovery"
	msgBdevFwrevStartMismatch = "controller fwrev unexpected before update"
	msgBdevFwrevEndMismatch   = "controller fwrev unchanged after update"
	msgBdevModelMismatch      = "controller model unexpected"
	msgBdevNoDevs             = "no controllers specified"
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

func (n *nvmeStorage) getController(pciAddr string) *pb.NvmeController {
	for _, c := range n.controllers {
		if c.Pciaddr == pciAddr {
			return c
		}
	}

	return nil
}

// Setup method implementation for nvmeStorage.
//
// Perform any setup to be performed before accessing NVMe devices.
// NOTE: doesn't attempt SPDK prep which requires elevated privileges,
//       that instead can be performed explicitly with subcommand.
func (n *nvmeStorage) Setup() error {
	resp := new(pb.ScanStorageResp)
	n.Discover(resp)

	if resp.Nvmestate.Status != pb.ResponseStatus_CTRL_SUCCESS {
		return errors.New("nvme scan: " + resp.Nvmestate.Error)
	}

	return nil
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
func (n *nvmeStorage) Discover(resp *pb.ScanStorageResp) {
	addStateDiscover := func(
		status pb.ResponseStatus, errMsg string,
		infoMsg string) *pb.ResponseState {

		return addState(
			status, errMsg, infoMsg, common.UtilLogDepth+1,
			"nvme storage discover")
	}

	if n.initialized {
		resp.Nvmestate = addStateDiscover(
			pb.ResponseStatus_CTRL_SUCCESS, "", "")
		resp.Ctrlrs = n.controllers
		return
	}

	// specify shmID to be set as opt in SPDK env init
	if err := n.env.InitSPDKEnv(n.config.NvmeShmID); err != nil {
		resp.Nvmestate = addStateDiscover(
			pb.ResponseStatus_CTRL_ERR_NVME,
			msgSpdkInitFail+": "+err.Error(), "")
		return
	}

	cs, ns, err := n.nvme.Discover()
	if err != nil {
		resp.Nvmestate = addStateDiscover(
			pb.ResponseStatus_CTRL_ERR_NVME,
			msgSpdkDiscoverFail+": "+err.Error(), "")
		return
	}
	n.controllers = loadControllers(cs, ns)

	resp.Nvmestate = addStateDiscover(
		pb.ResponseStatus_CTRL_SUCCESS, "", "")
	resp.Ctrlrs = n.controllers

	n.initialized = true
}

// newCret creates and populates NVMe controller result and logs error
// through addState.
func newCret(
	op string, pciaddr string, status pb.ResponseStatus, errMsg string,
	logDepth int) *pb.NvmeControllerResult {

	return &pb.NvmeControllerResult{
		Pciaddr: pciaddr,
		State: addState(
			status, errMsg, "", logDepth+1,
			"nvme controller "+op),
	}
}

// Format attempts to format (forcefully) NVMe devices on a given server
// as specified in config file and populates resp NvmeControllerResult for each
// NVMe controller specified in config file bdev_list param.
//
// One result with empty Pciaddr will be reported if there are preliminary
// errors occurring before devices could be accessed. Otherwise a result will
// be populated for each device in bdev_list.
func (n *nvmeStorage) Format(i int, results *([]*pb.NvmeControllerResult)) {
	var pciAddr string
	srv := n.config.Servers[i]
	log.Debugf("performing device format on NVMe controllers")

	// appends results to response to provide format specific function
	addCretFormat := func(status pb.ResponseStatus, errMsg string) {
		// log depth should be stack layer registering result
		*results = append(
			*results,
			newCret(
				"format", pciAddr, status, errMsg,
				common.UtilLogDepth+1))
	}

	if n.formatted {
		addCretFormat(
			pb.ResponseStatus_CTRL_ERR_APP,
			msgBdevAlreadyFormatted)
		return
	}

	switch srv.BdevClass {
	case bdNVMe:
		for _, pciAddr = range srv.BdevList {
			if pciAddr == "" {
				addCretFormat(
					pb.ResponseStatus_CTRL_ERR_CONF,
					msgBdevEmpty)
				continue
			}

			ctrlr := n.getController(pciAddr)
			if ctrlr == nil {
				addCretFormat(
					pb.ResponseStatus_CTRL_ERR_NVME,
					pciAddr+": "+msgBdevNotFound)
				continue
			}

			log.Debugf(
				"formatting nvme controller at %s, may take "+
					"several minutes!...", pciAddr)

			cs, ns, err := n.nvme.Format(pciAddr)
			if err != nil {
				addCretFormat(
					pb.ResponseStatus_CTRL_ERR_NVME,
					pciAddr+": "+err.Error())
				continue
			}

			log.Debugf(
				"controller format successful (%s)\n", pciAddr)

			addCretFormat(pb.ResponseStatus_CTRL_SUCCESS, "")
			n.controllers = loadControllers(cs, ns)
		}
	default:
		addCretFormat(
			pb.ResponseStatus_CTRL_ERR_CONF,
			string(srv.BdevClass)+": "+msgBdevClassNotSupported)
		return
	}

	// add info to result if no controllers have been formatted
	if len(*results) == 0 && len(srv.BdevList) == 0 {
		*results = append(
			*results,
			&pb.NvmeControllerResult{
				Pciaddr: "",
				State: addState(
					pb.ResponseStatus_CTRL_SUCCESS,
					"", "no controllers specified",
					common.UtilLogDepth,
					"nvme controller format"),
			})
	}

	log.Debugf("device format on NVMe controllers completed")
	n.formatted = true
	return
}

// Update attempts to update firmware on NVMe controllers attached to a
// given server identified by PCI addresses as specified in config file.
// Update populates resp NvmeControllerResult for each NVMe controller
// specified in config file bdev_list param.
//
// Firmware will only be updated if the controller the current fw rev
// and model match the "startRev" and "model" fn parameters respectively.
// Path and slot params refer to the fw image file location and controller
// firmware register to update respectively.
//
// One result with empty Pciaddr will be reported if there are preliminary
// errors occurring before devices could be accessed. Otherwise a result will
// be populated for each device in bdev_list.
func (n *nvmeStorage) Update(
	i int, req *pb.UpdateNvmeReq, results *([]*pb.NvmeControllerResult)) {

	var pciAddr string
	srv := n.config.Servers[i]
	log.Debugf("performing firmware update on NVMe controllers")

	// appends results to response to provide update specific function
	addCretUpdate := func(status pb.ResponseStatus, errMsg string) {
		// log depth should be stack layer registering result
		*results = append(
			*results,
			newCret(
				"update", pciAddr, status, errMsg,
				common.UtilLogDepth+1))
	}

	if !n.initialized {
		addCretUpdate(
			pb.ResponseStatus_CTRL_ERR_APP, msgBdevNotInited)
		return
	}

	switch srv.BdevClass {
	case bdNVMe:
		for _, pciAddr = range srv.BdevList {
			if pciAddr == "" {
				addCretUpdate(
					pb.ResponseStatus_CTRL_ERR_CONF,
					msgBdevEmpty)
				continue
			}

			ctrlr := n.getController(pciAddr)
			if ctrlr == nil {
				addCretUpdate(
					pb.ResponseStatus_CTRL_ERR_NVME,
					pciAddr+": "+msgBdevNotFound)
				continue
			}

			if strings.TrimSpace(ctrlr.Model) != req.Model {
				addCretUpdate(
					pb.ResponseStatus_CTRL_ERR_NVME,
					fmt.Sprintf(
						pciAddr+": "+
							msgBdevModelMismatch+
							" want %s, have %s",
						req.Model, ctrlr.Model))
				continue
			}

			if strings.TrimSpace(ctrlr.Fwrev) != req.Startrev {
				addCretUpdate(
					pb.ResponseStatus_CTRL_ERR_NVME,
					fmt.Sprintf(
						pciAddr+": "+
							msgBdevFwrevStartMismatch+
							" want %s, have %s",
						req.Startrev, ctrlr.Fwrev))
				continue
			}

			log.Debugf(
				"updating firmware (current rev %s, fw image %s)"+
					" on nvme controller at %s, may take several "+
					"minutes!", ctrlr.Fwrev, req.Path, pciAddr)

			cs, ns, err := n.nvme.Update(pciAddr, req.Path, req.Slot)
			if err != nil {
				addCretUpdate(
					pb.ResponseStatus_CTRL_ERR_NVME,
					fmt.Sprintf(
						pciAddr+": %T: "+err.Error(),
						n.nvme))
				// TODO: verify controller responsive after
				//       error, return fatal response to stop
				//       further updates if not
				continue
			}
			n.controllers = loadControllers(cs, ns)

			ctrlr = n.getController(pciAddr)
			if ctrlr == nil {
				addCretUpdate(
					pb.ResponseStatus_CTRL_ERR_NVME,
					pciAddr+": "+msgBdevNotFound+
						" (after update)")
				continue
			}

			// verify controller is reporting an updated rev
			if ctrlr.Fwrev == req.Startrev || ctrlr.Fwrev == "" {
				addCretUpdate(
					pb.ResponseStatus_CTRL_ERR_NVME,
					fmt.Sprintf(
						pciAddr+": "+
							msgBdevFwrevEndMismatch))
				continue
			}

			log.Debugf(
				"controller fwupdate successful (%s: %s->%s)\n",
				pciAddr, req.Startrev, ctrlr.Fwrev)

			addCretUpdate(pb.ResponseStatus_CTRL_SUCCESS, "")
		}
	default:
		addCretUpdate(
			pb.ResponseStatus_CTRL_ERR_CONF,
			string(srv.BdevClass)+": "+msgBdevClassNotSupported)
		return
	}

	log.Debugf("device fwupdates on specified NVMe controllers completed\n")
	return
}

// BurnIn method implementation for nvmeStorage
// Doesn't call through go-spdk, returns cmds to be issued over shell
func (n *nvmeStorage) BurnIn(pciAddr string, nsID int32, configPath string) (
	fioPath string, cmds []string, env string, err error) {

	if !n.initialized {
		err = errors.New(msgBdevNotInited)
		return
	}

	pluginDir := ""
	pluginDir, err = n.config.ext.getAbsInstallPath(spdkFioPluginDir)
	if err != nil {
		return
	}

	fioPath, err = n.config.ext.getAbsInstallPath(fioExecPath)
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
				Namespaces: loadNamespaces(c.PCIAddr, nss),
			})
	}
	return pbCtrlrs
}

// loadNamespaces converts slice of Namespace into protobuf equivalent.
// Implemented as a pure function.
func loadNamespaces(
	ctrlrPciAddr string, nss []spdk.Namespace) (
	_nss []*pb.NvmeController_Namespace) {

	for _, ns := range nss {
		if ns.CtrlrPciAddr == ctrlrPciAddr {
			_nss = append(
				_nss,
				&pb.NvmeController_Namespace{
					Id:       ns.ID,
					Capacity: ns.Size,
				})
		}
	}
	return
}

// newNvmeStorage creates a new instance of nvmeStorage struct.
func newNvmeStorage(config *configuration) (*nvmeStorage, error) {

	scriptPath, err := config.ext.getAbsInstallPath(spdkSetupPath)
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
