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

package server

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"strconv"

	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	spdkSetupPath      = "share/daos/control/setup_spdk.sh"
	defaultNrHugepages = 1024
	nrHugepagesEnv     = "_NRHUGE"
	targetUserEnv      = "_TARGET_USER"
	pciWhiteListEnv    = "_PCI_WHITELIST"

	msgBdevEmpty            = "bdev device list entry empty"
	msgBdevAlreadyFormatted = "nvme storage has already been formatted and " +
		"reformat not implemented"
	msgBdevNotFound = "controller at pci addr not found, check device exists " +
		"and can be discovered, you may need to run `sudo daos_server " +
		"storage prepare --nvme-only` to setup SPDK to access SSDs"
	msgBdevNotInited         = "nvme storage not initialized"
	msgBdevClassNotSupported = "operation unsupported on bdev class"
	msgSpdkInitFail          = "SPDK env init, has setup been run?"
	msgSpdkDiscoverFail      = "SPDK controller discovery"
	msgBdevNoDevs            = "no controllers specified"
	msgBdevClassIsFile       = "nvme emulation initialized with backend file"
	msgBdevScmNotReady       = "nvme format not performed because scm not ready"
)

// SpdkSetup is an interface to configure spdk prerequisites via a
// shell script
type SpdkSetup interface {
	prep(int, string, string) error
	reset() error
}

// spdkSetup is an implementation of the SpdkSetup interface
type spdkSetup struct {
	log         logging.Logger
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
	var stdout bytes.Buffer
	srv.Stdout = &stdout
	var stderr bytes.Buffer
	srv.Stderr = &stderr
	var hPages, tUsr, whitelist string

	if nrHugepages <= 0 {
		nrHugepages = defaultNrHugepages
	}
	hPages = nrHugepagesEnv + "=" + strconv.Itoa(nrHugepages)
	srv.Env = append(srv.Env, hPages)
	s.log.Debugf("spdk setup with %s\n", hPages)

	tUsr = targetUserEnv + "=" + usr
	srv.Env = append(srv.Env, tUsr)
	s.log.Debugf("spdk setup with %s\n", tUsr)

	if wlist != "" {
		whitelist = pciWhiteListEnv + "=" + wlist
		srv.Env = append(srv.Env, whitelist)
		s.log.Debugf("spdk setup with %s\n", whitelist)
	}

	if err := srv.Run(); err != nil {
		return errors.Wrapf(err, "spdk setup failed (%s, %s, %s, %s)",
			hPages, tUsr, whitelist, stderr.String())
	}

	s.log.Debugf("spdk setup run:\n%s", stdout.String())

	return nil
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
	log         logging.Logger
	ext         External
	env         spdk.ENV  // SPDK ENV interface
	nvme        spdk.NVME // SPDK NVMe interface
	spdk        SpdkSetup // SPDK shell configuration interface
	shmID       int
	controllers types.NvmeControllers
	initialized bool
	formatted   bool
}

func (n *nvmeStorage) hasControllers(pciAddrs []string) (missing []string, ok bool) {
	for _, addr := range pciAddrs {
		if n.getController(addr) == nil {
			missing = append(missing, addr)
		}
	}

	return missing, len(missing) == 0
}

func (n *nvmeStorage) getController(pciAddr string) *ctlpb.NvmeController {
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
	return n.Discover()
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
	if err := n.env.InitSPDKEnv(n.shmID); err != nil {
		return errors.WithMessage(err, msgSpdkInitFail)
	}

	cs, ns, dh, err := n.nvme.Discover()
	if err != nil {
		return errors.WithMessage(err, msgSpdkDiscoverFail)
	}
	n.controllers = loadControllers(cs, ns, dh)
	n.initialized = true

	return nil
}

// newCret creates and populates NVMe controller result and logs error
func newCret(log logging.Logger, op string, pciaddr string, status ctlpb.ResponseStatus, errMsg string,
	infoMsg string) *ctlpb.NvmeControllerResult {

	return &ctlpb.NvmeControllerResult{
		Pciaddr: pciaddr,
		State:   newState(log, status, errMsg, infoMsg, "nvme controller "+op),
	}
}

// Format attempts to format (forcefully) NVMe devices on a given server
// as specified in config file and populates resp NvmeControllerResult for each
// NVMe controller specified in config file bdev_list param.
//
// One result with empty Pciaddr will be reported if there are preliminary
// errors occurring before devices could be accessed. Otherwise a result will
// be populated for each device in bdev_list.
func (n *nvmeStorage) Format(cfg storage.BdevConfig, results *(types.NvmeControllerResults)) {
	var pciAddr string
	n.log.Debugf("performing device format on NVMe controllers")

	// appends results to response to provide format specific function
	addCretFormat := func(status ctlpb.ResponseStatus, errMsg string, infoMsg string) {
		*results = append(*results,
			newCret(n.log, "format", pciAddr, status, errMsg, infoMsg))
	}

	if n.formatted {
		addCretFormat(ctlpb.ResponseStatus_CTL_ERR_APP, msgBdevAlreadyFormatted, "")
		return
	}

	switch cfg.Class {
	case storage.BdevClassMalloc:
		n.log.Debugf("malloc bdev format successful (%s)\n", pciAddr)
		addCretFormat(ctlpb.ResponseStatus_CTL_SUCCESS, "", "")
	case storage.BdevClassKdev:
		n.log.Debugf("kernel bdev format successful (%s)\n", pciAddr)
		addCretFormat(ctlpb.ResponseStatus_CTL_SUCCESS, "", "")
	case storage.BdevClassFile:
		n.log.Debugf("bdev file format successful (%s)\n", pciAddr)
		addCretFormat(ctlpb.ResponseStatus_CTL_SUCCESS, "", msgBdevClassIsFile)
	case storage.BdevClassNvme:
		for _, pciAddr = range cfg.DeviceList {
			if pciAddr == "" {
				addCretFormat(ctlpb.ResponseStatus_CTL_ERR_CONF,
					msgBdevEmpty, "")
				continue
			}

			ctrlr := n.getController(pciAddr)
			if ctrlr == nil {
				addCretFormat(ctlpb.ResponseStatus_CTL_ERR_NVME,
					pciAddr+": "+msgBdevNotFound, "")
				continue
			}

			n.log.Debugf("formatting nvme controller at %s, may take "+
				"several minutes!...", pciAddr)

			cs, ns, err := n.nvme.Format(pciAddr)
			if err != nil {
				addCretFormat(ctlpb.ResponseStatus_CTL_ERR_NVME,
					pciAddr+": "+err.Error(), "")
				continue
			}

			n.log.Debugf("controller format successful (%s)\n", pciAddr)

			addCretFormat(ctlpb.ResponseStatus_CTL_SUCCESS, "", "")
			n.controllers = loadControllers(cs, ns, nil)
		}
	default:
		addCretFormat(ctlpb.ResponseStatus_CTL_ERR_CONF,
			fmt.Sprintf("%s: %s", cfg.Class, msgBdevClassNotSupported), "")
		return
	}

	// add info to result if no controllers have been formatted
	if len(*results) == 0 && len(cfg.DeviceList) == 0 {
		addCretFormat(ctlpb.ResponseStatus_CTL_SUCCESS,
			"", msgBdevNoDevs)
	}

	n.log.Debugf("device format on NVMe controllers completed")
	n.formatted = true
}

// loadControllers converts slice of Controller into protobuf equivalent.
// Implemented as a pure function.
func loadControllers(ctrlrs []spdk.Controller, nss []spdk.Namespace,
	healthStats []spdk.DeviceHealth) (pbCtrlrs types.NvmeControllers) {

	for _, c := range ctrlrs {
		pbCtrlrs = append(
			pbCtrlrs,
			&ctlpb.NvmeController{
				Model:       c.Model,
				Serial:      c.Serial,
				Pciaddr:     c.PCIAddr,
				Fwrev:       c.FWRev,
				Socketid:    c.SocketID,
				Healthstats: loadHealthStats(c.PCIAddr, healthStats),
				Namespaces:  loadNamespaces(c.PCIAddr, nss), // repeated pb field
			})
	}
	return pbCtrlrs
}

// loadNamespaces converts slice of Namespace into protobuf equivalent.
// Implemented as a pure function.
func loadNamespaces(ctrlrPciAddr string, nss []spdk.Namespace) (_nss types.NvmeNamespaces) {
	for _, ns := range nss {
		if ns.CtrlrPciAddr == ctrlrPciAddr {
			_nss = append(
				_nss,
				&ctlpb.NvmeController_Namespace{
					Id:   ns.ID,
					Size: ns.Size,
				})
		}
	}
	return
}

// loadHealthStats find health statistics for a given control identified by PCI
// address.
func loadHealthStats(ctrlrPciAddr string, hss []spdk.DeviceHealth) *ctlpb.NvmeController_Health {
	for _, hs := range hss {
		if hs.CtrlrPciAddr == ctrlrPciAddr {
			return &ctlpb.NvmeController_Health{
				Temp:            hs.Temp,
				Tempwarntime:    hs.TempWarnTime,
				Tempcrittime:    hs.TempCritTime,
				Ctrlbusytime:    hs.CtrlBusyTime,
				Powercycles:     hs.PowerCycles,
				Poweronhours:    hs.PowerOnHours,
				Unsafeshutdowns: hs.UnsafeShutdowns,
				Mediaerrors:     hs.MediaErrors,
				Errorlogentries: hs.ErrorLogEntries,
				Tempwarn:        hs.TempWarn,
				Availsparewarn:  hs.AvailSpareWarn,
				Reliabilitywarn: hs.ReliabilityWarn,
				Readonlywarn:    hs.ReadOnlyWarn,
				Volatilewarn:    hs.VolatileWarn,
			}
		}
	}

	return nil // none found
}

// newNvmeStorage creates a new instance of nvmeStorage struct.
func newNvmeStorage(log logging.Logger, shmID int, spdkScript *spdkSetup, ext External) *nvmeStorage {
	return &nvmeStorage{
		log:   log,
		ext:   ext,
		spdk:  spdkScript,
		env:   &spdk.Env{},
		nvme:  &spdk.Nvme{},
		shmID: shmID,
	}
}
