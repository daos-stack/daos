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

package storage

import (
	"fmt"
	"sync"

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/storage"
	. "github.com/daos-stack/daos/src/control/lib/ipmctl"
	. "github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
)

// MockController is a mock NVMe SSD controller of type exported from go-spdk.
func MockController(fwrev string) Controller {
	c := MockControllerPB(fwrev)
	return Controller{
		Model:    c.Model,
		Serial:   c.Serial,
		PCIAddr:  c.Pciaddr,
		FWRev:    fwrev,
		SocketID: c.Socketid,
	}
}

// NewMockController specifies customer details fr a mock NVMe SSD controller.
func NewMockController(
	pciaddr string, fwrev string, model string, serial string, socketID int32,
) Controller {
	return Controller{
		Model:    model,
		Serial:   serial,
		PCIAddr:  pciaddr,
		FWRev:    fwrev,
		SocketID: socketID,
	}
}

// MockNamespace is a mock NVMe SSD namespace of type exported from go-spdk.
func MockNamespace(ctrlr *Controller) Namespace {
	n := MockNamespacePB()
	return Namespace{
		ID:           n.Id,
		Size:         n.Capacity,
		CtrlrPciAddr: ctrlr.PCIAddr,
	}
}

// MockDeviceHealth is a mock NVMe SSD device health of type exported from go-spdk.
func MockDeviceHealth(ctrlr *Controller) DeviceHealth {
	h := MockDeviceHealthPB()
	return DeviceHealth{
		Temp:            h.Temp,
		TempWarnTime:    h.Tempwarn,
		TempCritTime:    h.Tempcrit,
		CtrlBusyTime:    h.Ctrlbusy,
		PowerCycles:     h.Powercycles,
		PowerOnHours:    h.Poweronhours,
		UnsafeShutdowns: h.Unsafeshutdowns,
		MediaErrors:     h.Mediaerrors,
		ErrorLogEntries: h.Errorlogs,
		TempWarn:        h.Tempwarning,
		AvailSpareWarn:  h.Availspare,
		ReliabilityWarn: h.Reliability,
		ReadOnlyWarn:    h.Readonly,
		VolatileWarn:    h.Volatilemem,
	}
}

// mock external interface implementations for daos/src/control/lib/spdk package
type mockSpdkEnv struct {
	initRet error // ENV interface InitSPDKEnv() return value
}

func (m *mockSpdkEnv) InitSPDKEnv(int) error { return m.initRet }

func NewMockSpdkEnv(initRet error) ENV { return &mockSpdkEnv{initRet} }

func DefaultMockSpdkEnv() ENV { return NewMockSpdkEnv(nil) }

// mock external interface implementations for daos/src/control/lib/nvme package
type mockSpdkNvme struct {
	log          logging.Logger
	fwRevBefore  string
	fwRevAfter   string
	initCtrlrs   []Controller
	initNss      []Namespace
	initHealth   []DeviceHealth
	discoverRet  error // NVME interface Discover() return value
	devFormatRet error // NVME interface Format() return value
	updateRet    error // NVME interface Update() return value
	formatCalls  []string
}

// Discover mock implementation returns mock lists of devices
func (m *mockSpdkNvme) Discover() ([]Controller, []Namespace, []DeviceHealth, error) {
	return m.initCtrlrs, m.initNss, m.initHealth, m.discoverRet
}

// Format mock implementation records calls on devices with given pci address
func (m *mockSpdkNvme) Format(pciAddr string) ([]Controller, []Namespace, error) {
	if m.devFormatRet == nil {
		m.formatCalls = append(m.formatCalls, pciAddr)
	}

	return m.initCtrlrs, m.initNss, m.devFormatRet
}

// Update mock implementation modifies Fwrev of device with given pci address
func (m *mockSpdkNvme) Update(pciAddr string, path string, slot int32) (
	[]Controller, []Namespace, error) {

	for i, ctrlr := range m.initCtrlrs {
		if ctrlr.PCIAddr == pciAddr && m.updateRet == nil {
			m.initCtrlrs[i].FWRev = m.fwRevAfter
		}
	}

	return m.initCtrlrs, m.initNss, m.updateRet
}

func (m *mockSpdkNvme) Cleanup() {}

func NewMockSpdkNvme(
	log logging.Logger,
	fwBefore string, fwAfter string,
	ctrlrs []Controller, nss []Namespace, dh []DeviceHealth,
	discoverRet error, devFormatRet error, updateRet error) NVME {

	return &mockSpdkNvme{
		log, fwBefore, fwAfter, ctrlrs, nss, dh,
		discoverRet, devFormatRet, updateRet, []string{},
	}
}

func DefaultMockSpdkNvme(log logging.Logger) NVME {
	c := MockController("1.0.0")

	return NewMockSpdkNvme(
		log,
		"1.0.0", "1.0.1",
		[]Controller{c}, []Namespace{MockNamespace(&c)},
		[]DeviceHealth{MockDeviceHealth(&c)},
		nil, nil, nil)
}

// mock external interface implementations for spdk setup script
// TODO: provide capability to return values from mock
type mockSpdkSetup struct{}

func (m *mockSpdkSetup) prep(int, string, string) error { return nil }
func (m *mockSpdkSetup) reset() error                   { return nil }

// mockNvmeStorage factory
func NewMockNvmeStorage(
	log logging.Logger,
	spdkEnv ENV, spdkNvme NVME, inited bool) *NvmeProvider {

	return &NvmeProvider{
		log:         log,
		ext:         defaultNvmeExt(),
		env:         spdkEnv,
		nvme:        spdkNvme,
		spdk:        &mockSpdkSetup{},
		initialized: inited,
	}
}

// defaultMockNvmeStorage factory
func DefaultMockNvmeStorage(log logging.Logger) *NvmeProvider {
	return NewMockNvmeStorage(
		log,
		DefaultMockSpdkEnv(),
		DefaultMockSpdkNvme(log),
		false) // Discover will not fetch when initialised is true
}

// MockModule returns a mock SCM module of type exported from ipmctl.
func MockModule() DeviceDiscovery {
	m := MockModulePB()
	dd := DeviceDiscovery{}
	dd.Physical_id = uint16(m.Physicalid)
	dd.Channel_id = uint16(m.Loc.Channel)
	dd.Channel_pos = uint16(m.Loc.Channelpos)
	dd.Memory_controller_id = uint16(m.Loc.Memctrlr)
	dd.Socket_id = uint16(m.Loc.Socket)
	dd.Capacity = m.Capacity

	return dd
}

type mockIpmctl struct {
	discoverModulesRet error
	modules            []DeviceDiscovery
}

func (m *mockIpmctl) Discover() ([]DeviceDiscovery, error) {
	return m.modules, m.discoverModulesRet
}

// ScmStorage factory with mocked interfaces for testing
func NewMockScmProvider(log logging.Logger, ext scmExt, discoverModulesRet error,
	mms []DeviceDiscovery, inited bool, prep PrepScm) *ScmProvider {
	if ext == nil {
		ext = &MockScmExt{}
		fmt.Printf("ext was nil\n")
	}

	return &ScmProvider{
		ext:         ext,
		log:         log,
		ipmctl:      &mockIpmctl{discoverModulesRet, mms},
		prep:        prep,
		initialized: inited,
	}
}

func DefaultMockScmProvider(log logging.Logger, ext scmExt) *ScmProvider {
	m := MockModule()

	return NewMockScmProvider(log, ext, nil, []DeviceDiscovery{m}, false, NewMockPrepScm())
}

// MockPmemDevice returns a mock pmem kernel device.
func MockPmemDevice() PmemDev {
	pmdPB := MockPmemDevicePB()

	return PmemDev{pmdPB.Uuid, pmdPB.Blockdev, pmdPB.Dev, int(pmdPB.Numanode)}
}

// mock implementation of PrepScm interface for external testing
type MockPrepScm struct {
	PmemDevs         []PmemDev
	prepNeedsReboot  bool
	resetNeedsReboot bool
	prepRet          error
	resetRet         error
	currentState     ScmState
	getStateRet      error
	getNamespacesRet error
}

func (mp *MockPrepScm) Prep(ScmState) (bool, []PmemDev, error) {
	return mp.prepNeedsReboot, mp.PmemDevs, mp.prepRet
}
func (mp *MockPrepScm) PrepReset(ScmState) (bool, error) {
	return mp.resetNeedsReboot, mp.resetRet
}
func (mp *MockPrepScm) GetState() (ScmState, error) {
	return mp.currentState, mp.getStateRet
}
func (mp *MockPrepScm) GetNamespaces() ([]PmemDev, error) {
	return mp.PmemDevs, mp.getNamespacesRet
}

func NewMockPrepScm() PrepScm {
	return &MockPrepScm{}
}

func NewMockScmExt(mounted bool, cmd, mount, unmount, mkdir, remove error) *MockScmExt {
	return &MockScmExt{
		isMountPointRet: mounted,
		cmdRet:          cmd,
		mountRet:        mount,
		unmountRet:      unmount,
		mkdirRet:        mkdir,
		removeRet:       remove,
	}
}

type MockScmExt struct {
	sync.RWMutex
	history []string

	isMountPointRet bool

	cmdRet     error
	mountRet   error
	unmountRet error
	mkdirRet   error
	removeRet  error
}

func (e *MockScmExt) appendHistory(str string) {
	e.Lock()
	defer e.Unlock()
	e.history = append(e.history, str)
}

func (e MockScmExt) getHistory() []string {
	e.RLock()
	defer e.RUnlock()
	return e.history
}

func (m *MockScmExt) runCommand(cmd string) error {
	m.appendHistory(fmt.Sprintf(msgCmd, cmd))

	return m.cmdRet
}

func (m *MockScmExt) isMountPoint(path string) (bool, error) {
	m.appendHistory(fmt.Sprintf(msgIsMountPoint, path))

	return m.isMountPointRet, nil
}

func (m *MockScmExt) unmount(path string) error {
	m.appendHistory(fmt.Sprintf(msgUnmount, path))

	return m.unmountRet
}

func (e *MockScmExt) mount(dev, mnt, typ string, flags uintptr, opts string) error {
	op := fmt.Sprintf(msgMount, dev, mnt, typ, fmt.Sprint(flags), opts)

	e.appendHistory(op)

	return e.mountRet
}

func (m *MockScmExt) mkdir(path string) error {
	m.appendHistory(fmt.Sprintf(msgMkdir, path))

	return m.mkdirRet
}

func (m *MockScmExt) remove(path string) error {
	m.appendHistory(fmt.Sprintf(msgRemove, path))

	return m.removeRet
}
