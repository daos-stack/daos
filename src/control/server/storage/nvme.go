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

package storage

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/server/storage/config"
	. "github.com/daos-stack/daos/src/control/server/storage/messages"
)

const (
	SpdkFioPluginDir   = "share/daos/spdk/fio_plugin"
	spdkSetupPath      = "share/daos/control/setup_spdk.sh"
	fioExecPath        = "bin/fio"
	defaultNrHugepages = 1024
	nrHugepagesEnv     = "_NRHUGE"
	targetUserEnv      = "_TARGET_USER"
	pciWhiteListEnv    = "_PCI_WHITELIST"
)

type nvmeExt interface {
	getAbsInstallPath(path string) (string, error)
}

type nvmeExtImpl struct{}

func (e *nvmeExtImpl) getAbsInstallPath(path string) (string, error) {
	return common.GetAbsInstallPath(path)
}

func defaultNvmeExt() *nvmeExtImpl {
	return &nvmeExtImpl{}
}

// SpdkSetup is an interface to configure spdk prerequisites via a
// shell script
type SpdkSetup interface {
	prep(nrHugePages int, user, whiteList string) error
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
func (s *spdkSetup) prep(nrHugepages int, usr, wlist string) error {
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
	s.log.Debugf("spdk setup with %s\n", hPages)

	tUsr = targetUserEnv + "=" + usr
	srv.Env = append(srv.Env, tUsr)
	s.log.Debugf("spdk setup with %s\n", tUsr)

	if wlist != "" {
		whitelist = pciWhiteListEnv + "=" + wlist
		srv.Env = append(srv.Env, whitelist)
		s.log.Debugf("spdk setup with %s\n", whitelist)
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

// NvmeProvider gives access to underlying SPDK interfaces
// for accessing Nvme devices (API) as well as storing device
// details.
type NvmeProvider struct {
	log         logging.Logger
	ext         nvmeExt
	env         spdk.ENV  // SPDK ENV interface
	nvme        spdk.NVME // SPDK NVMe interface
	spdk        SpdkSetup // SPDK shell configuration interface
	shmID       int
	controllers types.NvmeControllers
	initialized bool
	formatted   bool
}

func (n *NvmeProvider) ResetSpdk() error {
	return n.spdk.reset()
}

func (n *NvmeProvider) PrepSpdk(nrHugePages int, user, whitelist string) error {
	return n.spdk.prep(nrHugePages, user, whitelist)
}

func (n *NvmeProvider) HasControllers(pciAddrs []string) (missing []string, ok bool) {
	for _, addr := range pciAddrs {
		if n.getController(addr) == nil {
			missing = append(missing, addr)
		}
	}

	return missing, len(missing) == 0
}

func (n *NvmeProvider) getController(pciAddr string) *ctlpb.NvmeController {
	for _, c := range n.controllers {
		if c.Pciaddr == pciAddr {
			return c
		}
	}

	return nil
}

// Setup method implementation for NvmeProvider.
//
// Perform any setup to be performed before accessing NVMe devices.
// NOTE: doesn't attempt SPDK prep which requires elevated privileges,
//       that instead can be performed explicitly with subcommand.
func (n *NvmeProvider) Setup() error {
	return n.Discover()
}

// Teardown method implementation for NvmeProvider.
//
// Perform any teardown to be performed after accessing NVMe devices.
func (n *NvmeProvider) Teardown() (err error) {
	// Cleanup references to NVMe devices held by go-spdk bindings
	n.nvme.Cleanup()
	// TODO: Decide whether to rebind PCI devices back to their original
	// drivers and release hugepages here.
	// err = n.setup.reset()
	n.initialized = false

	return
}

// Discover method implementation for NvmeProvider.
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
func (n *NvmeProvider) Discover() error {
	if n.initialized {
		return nil
	}

	// specify shmID to be set as opt in SPDK env init
	if err := n.env.InitSPDKEnv(n.shmID); err != nil {
		return errors.WithMessage(err, MsgSpdkInitFail)
	}

	cs, ns, dh, err := n.nvme.Discover()
	if err != nil {
		return errors.WithMessage(err, MsgSpdkDiscoverFail)
	}
	n.controllers = loadControllers(cs, ns, dh)
	n.initialized = true

	return nil
}

func (n *NvmeProvider) Controllers() types.NvmeControllers {
	return n.controllers
}

func (n *NvmeProvider) Initialized() bool {
	return n.initialized
}

func (n *NvmeProvider) SetFormatted(f bool) {
	n.formatted = f
}

func (n *NvmeProvider) Formatted() bool {
	return n.formatted
}

// newCret creates and populates NVMe controller result and logs error
func newCret(log logging.Logger, op string, pciaddr string, status ctlpb.ResponseStatus, errMsg string,
	infoMsg string) *ctlpb.NvmeControllerResult {

	return &ctlpb.NvmeControllerResult{
		Pciaddr: pciaddr,
		State:   NewState(log, status, errMsg, infoMsg, "nvme controller "+op),
	}
}

// Format attempts to format (forcefully) NVMe devices on a given server
// as specified in config file and populates resp NvmeControllerResult for each
// NVMe controller specified in config file bdev_list param.
//
// One result with empty Pciaddr will be reported if there are preliminary
// errors occurring before devices could be accessed. Otherwise a result will
// be populated for each device in bdev_list.
func (n *NvmeProvider) Format(cfg BdevConfig, results *(types.NvmeControllerResults)) {
	var pciAddr string
	n.log.Debugf("performing device format on NVMe controllers")

	// appends results to response to provide format specific function
	addCretFormat := func(status ctlpb.ResponseStatus, errMsg string, infoMsg string) {
		*results = append(*results,
			newCret(n.log, "format", pciAddr, status, errMsg, infoMsg))
	}

	if n.formatted {
		addCretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, MsgBdevAlreadyFormatted, "")
		return
	}

	switch cfg.Class {
	case BdevClassMalloc:
		n.log.Debugf("malloc bdev format successful (%s)\n", pciAddr)
		addCretFormat(ctlpb.ResponseStatus_CTRL_SUCCESS, "", "")
	case BdevClassKdev:
		n.log.Debugf("kernel bdev format successful (%s)\n", pciAddr)
		addCretFormat(ctlpb.ResponseStatus_CTRL_SUCCESS, "", "")
	case BdevClassFile:
		n.log.Debugf("bdev file format successful (%s)\n", pciAddr)
		addCretFormat(ctlpb.ResponseStatus_CTRL_SUCCESS, "", MsgBdevClassIsFile)
	case BdevClassNvme:
		for _, pciAddr = range cfg.DeviceList {
			if pciAddr == "" {
				addCretFormat(ctlpb.ResponseStatus_CTRL_ERR_CONF,
					MsgBdevEmpty, "")
				continue
			}

			ctrlr := n.getController(pciAddr)
			if ctrlr == nil {
				addCretFormat(ctlpb.ResponseStatus_CTRL_ERR_NVME,
					pciAddr+": "+MsgBdevNotFound, "")
				continue
			}

			n.log.Debugf("formatting nvme controller at %s, may take "+
				"several minutes!...", pciAddr)

			cs, ns, err := n.nvme.Format(pciAddr)
			if err != nil {
				addCretFormat(ctlpb.ResponseStatus_CTRL_ERR_NVME,
					pciAddr+": "+err.Error(), "")
				continue
			}

			n.log.Debugf("controller format successful (%s)\n", pciAddr)

			addCretFormat(ctlpb.ResponseStatus_CTRL_SUCCESS, "", "")
			n.controllers = loadControllers(cs, ns, nil)
		}
	default:
		addCretFormat(ctlpb.ResponseStatus_CTRL_ERR_CONF,
			fmt.Sprintf("%s: %s", cfg.Class, MsgBdevClassNotSupported), "")
		return
	}

	// add info to result if no controllers have been formatted
	if len(*results) == 0 && len(cfg.DeviceList) == 0 {
		addCretFormat(ctlpb.ResponseStatus_CTRL_SUCCESS,
			"", "no controllers specified")
	}

	n.log.Debugf("device format on NVMe controllers completed")
	n.formatted = true
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
func (n *NvmeProvider) Update(cfg BdevConfig, req *ctlpb.UpdateNvmeReq, results *(types.NvmeControllerResults)) {
	var pciAddr string
	n.log.Debugf("performing firmware update on NVMe controllers")

	// appends results to response to provide update specific function
	addCretUpdate := func(status ctlpb.ResponseStatus, errMsg string) {
		*results = append(*results, newCret(n.log, "update", pciAddr, status, errMsg, ""))
	}

	if !n.initialized {
		addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_APP, MsgBdevNotInited)
		return
	}

	switch cfg.Class {
	case BdevClassNvme:
		for _, pciAddr = range cfg.DeviceList {
			if pciAddr == "" {
				addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_CONF, MsgBdevEmpty)
				continue
			}

			ctrlr := n.getController(pciAddr)
			if ctrlr == nil {
				addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_NVME,
					pciAddr+": "+MsgBdevNotFound)
				continue
			}

			if strings.TrimSpace(ctrlr.Model) != req.Model {
				addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_NVME,
					fmt.Sprintf(pciAddr+": "+MsgBdevModelMismatch+
						" want %s, have %s", req.Model, ctrlr.Model))
				continue
			}

			if strings.TrimSpace(ctrlr.Fwrev) != req.Startrev {
				addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_NVME,
					fmt.Sprintf(pciAddr+": "+MsgBdevFwrevStartMismatch+
						" want %s, have %s", req.Startrev, ctrlr.Fwrev))
				continue
			}

			n.log.Debugf(
				"updating firmware (current rev %s, fw image %s)"+
					" on nvme controller at %s, may take several "+
					"minutes!", ctrlr.Fwrev, req.Path, pciAddr)

			cs, ns, err := n.nvme.Update(pciAddr, req.Path, req.Slot)
			if err != nil {
				addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_NVME,
					fmt.Sprintf(pciAddr+": %T: "+err.Error(), n.nvme))
				// TODO: verify controller responsive after
				//       error, return fatal response to stop
				//       further updates if not
				continue
			}
			n.controllers = loadControllers(cs, ns, nil)

			ctrlr = n.getController(pciAddr)
			if ctrlr == nil {
				addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_NVME,
					pciAddr+": "+MsgBdevNotFound+" (after update)")
				continue
			}

			// verify controller is reporting an updated rev
			if ctrlr.Fwrev == req.Startrev || ctrlr.Fwrev == "" {
				addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_NVME,
					fmt.Sprintf(pciAddr+": "+MsgBdevFwrevEndMismatch))
				continue
			}

			n.log.Debugf("controller fwupdate successful (%s: %s->%s)\n",
				pciAddr, req.Startrev, ctrlr.Fwrev)

			addCretUpdate(ctlpb.ResponseStatus_CTRL_SUCCESS, "")
		}
	default:
		addCretUpdate(ctlpb.ResponseStatus_CTRL_ERR_CONF,
			fmt.Sprintf("%s: %s", cfg.Class, MsgBdevClassNotSupported))
		return
	}

	n.log.Debugf("device fwupdates on specified NVMe controllers completed\n")
}

// BurnIn method implementation for NvmeProvider
// Doesn't call through go-spdk, returns cmds to be issued over shell
func (n *NvmeProvider) BurnIn(pciAddr string, nsID int32, configPath string) (
	fioPath string, cmds []string, env string, err error) {

	if !n.initialized {
		err = errors.New(MsgBdevNotInited)
		return
	}

	pluginDir := ""
	pluginDir, err = n.ext.getAbsInstallPath(SpdkFioPluginDir)
	if err != nil {
		return
	}

	fioPath, err = n.ext.getAbsInstallPath(fioExecPath)
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
	n.log.Debugf(
		"BurnIn command string: %s %s %v", env, fioPath, cmds)

	return
}

// loadControllers converts slice of Controller into protobuf equivalent.
// Implemented as a pure function.
func loadControllers(ctrlrs []spdk.Controller, nss []spdk.Namespace,
	health []spdk.DeviceHealth) (pbCtrlrs types.NvmeControllers) {

	for _, c := range ctrlrs {
		pbCtrlrs = append(
			pbCtrlrs,
			&ctlpb.NvmeController{
				Model:    c.Model,
				Serial:   c.Serial,
				Pciaddr:  c.PCIAddr,
				Fwrev:    c.FWRev,
				Socketid: c.SocketID,
				// repeated pb field
				Namespaces:  loadNamespaces(c.PCIAddr, nss),
				Healthstats: loadHealthStats(health),
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
					Id:       ns.ID,
					Capacity: ns.Size,
				})
		}
	}
	return
}

// loadHealthStats converts a slice of DeviceHealth into protobuf equivalent.
// Implemented as a pure function.
func loadHealthStats(health []spdk.DeviceHealth) (_health types.NvmeHealthstats) {
	for _, h := range health {
		_health = append(
			_health,
			&ctlpb.NvmeController_Health{
				Temp:            h.Temp,
				Tempwarn:        h.TempWarnTime,
				Tempcrit:        h.TempCritTime,
				Ctrlbusy:        h.CtrlBusyTime,
				Powercycles:     h.PowerCycles,
				Poweronhours:    h.PowerOnHours,
				Unsafeshutdowns: h.UnsafeShutdowns,
				Mediaerrors:     h.MediaErrors,
				Errorlogs:       h.ErrorLogEntries,
				Tempwarning:     h.TempWarn,
				Availspare:      h.AvailSpareWarn,
				Reliability:     h.ReliabilityWarn,
				Readonly:        h.ReadOnlyWarn,
				Volatilemem:     h.VolatileWarn,
			})
	}

	return
}

// NewNvmeProvider creates a new instance of NvmeProvider struct.
func NewNvmeProvider(log logging.Logger, cfg BdevConfig) (*NvmeProvider, error) {
	ext := defaultNvmeExt()

	scriptPath, err := ext.getAbsInstallPath(spdkSetupPath)
	if err != nil {
		return nil, err
	}

	spdkScript := &spdkSetup{
		log:         log,
		scriptPath:  scriptPath,
		nrHugePages: cfg.HugePageCount,
	}

	return &NvmeProvider{
		log:   log,
		ext:   ext,
		spdk:  spdkScript,
		env:   &spdk.Env{},
		nvme:  &spdk.Nvme{},
		shmID: cfg.ShmID,
	}, nil
}
