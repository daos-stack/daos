//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"sync"

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/mount"
)

type (
	mockIpmctlCfg struct {
		initErr           error
		getModulesErr     error
		modules           []ipmctl.DeviceDiscovery
		delGoalsErr       error
		getRegionsErr     error
		regions           []ipmctl.PMemRegion
		getFWInfoRet      error
		fwInfo            ipmctl.DeviceFirmwareInfo
		updateFirmwareRet error
	}

	mockIpmctl struct {
		cfg mockIpmctlCfg
	}
)

func (m *mockIpmctl) Init(_ logging.Logger) error {
	return m.cfg.initErr
}

func (m *mockIpmctl) GetModules(_ logging.Logger) ([]ipmctl.DeviceDiscovery, error) {
	return m.cfg.modules, m.cfg.getModulesErr
}

func (m *mockIpmctl) DeleteConfigGoals(_ logging.Logger) error {
	return m.cfg.delGoalsErr
}

func (m *mockIpmctl) GetRegions(_ logging.Logger) ([]ipmctl.PMemRegion, error) {
	return m.cfg.regions, m.cfg.getRegionsErr
}

func (m *mockIpmctl) GetFirmwareInfo(uid ipmctl.DeviceUID) (ipmctl.DeviceFirmwareInfo, error) {
	return m.cfg.fwInfo, m.cfg.getFWInfoRet
}

func (m *mockIpmctl) UpdateFirmware(uid ipmctl.DeviceUID, fwPath string, force bool) error {
	return m.cfg.updateFirmwareRet
}

func newMockIpmctl(cfg *mockIpmctlCfg) *mockIpmctl {
	if cfg == nil {
		cfg = &mockIpmctlCfg{}
	}

	return &mockIpmctl{
		cfg: *cfg,
	}
}

// mockDiscovery returns a mock SCM module of type exported from ipmctl.
func mockDiscovery(sockID ...int) ipmctl.DeviceDiscovery {
	m := proto.MockScmModule()

	sid := m.Socketid
	if len(sockID) > 0 {
		sid = uint32(sockID[0])
	}

	result := ipmctl.DeviceDiscovery{
		Physical_id:          uint16(m.Physicalid),
		Channel_id:           uint16(m.Channelid),
		Channel_pos:          uint16(m.Channelposition),
		Memory_controller_id: uint16(m.Controllerid),
		Socket_id:            uint16(sid),
		Capacity:             m.Capacity,
	}

	_ = copy(result.Uid[:], m.Uid)
	_ = copy(result.Part_number[:], m.PartNumber)
	_ = copy(result.Fw_revision[:], m.FirmwareRevision)

	return result
}

// mockModule converts ipmctl type SCM module and returns storage/scm
// internal type.
func mockModule(dIn ...ipmctl.DeviceDiscovery) *storage.ScmModule {
	d := mockDiscovery()
	if len(dIn) > 0 {
		d = dIn[0]
	}

	return &storage.ScmModule{
		PhysicalID:       uint32(d.Physical_id),
		ChannelID:        uint32(d.Channel_id),
		ChannelPosition:  uint32(d.Channel_pos),
		ControllerID:     uint32(d.Memory_controller_id),
		SocketID:         uint32(d.Socket_id),
		Capacity:         d.Capacity,
		UID:              d.Uid.String(),
		PartNumber:       d.Part_number.String(),
		FirmwareRevision: d.Fw_revision.String(),
	}
}

// MockBackendConfig specifies behavior for a mock SCM backend
// implementation providing capability to access and configure
// SCM modules and namespaces.
type MockBackendConfig struct {
	GetModulesRes        storage.ScmModules
	GetModulesErr        error
	GetNamespacesRes     storage.ScmNamespaces
	GetNamespacesErr     error
	PrepRes              *storage.ScmPrepareResponse
	PrepErr              error
	PrepResetRes         *storage.ScmPrepareResponse
	PrepResetErr         error
	GetFirmwareStatusErr error
	GetFirmwareStatusRes *storage.ScmFirmwareInfo
	UpdateFirmwareErr    error
}

type MockBackend struct {
	sync.RWMutex
	cfg          MockBackendConfig
	PrepareCalls []storage.ScmPrepareRequest
	ResetCalls   []storage.ScmPrepareRequest
}

func (mb *MockBackend) getModules(int) (storage.ScmModules, error) {
	return mb.cfg.GetModulesRes, mb.cfg.GetModulesErr
}

func (mb *MockBackend) getNamespaces(int) (storage.ScmNamespaces, error) {
	return mb.cfg.GetNamespacesRes, mb.cfg.GetNamespacesErr
}

func (mb *MockBackend) prep(req storage.ScmPrepareRequest, _ *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	mb.Lock()
	mb.PrepareCalls = append(mb.PrepareCalls, req)
	mb.Unlock()

	if mb.cfg.PrepErr != nil {
		return nil, mb.cfg.PrepErr
	} else if mb.cfg.PrepRes == nil {
		return &storage.ScmPrepareResponse{}, nil
	}
	return mb.cfg.PrepRes, mb.cfg.PrepErr
}

func (mb *MockBackend) prepReset(req storage.ScmPrepareRequest, _ *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	mb.Lock()
	mb.ResetCalls = append(mb.ResetCalls, req)
	mb.Unlock()

	if mb.cfg.PrepResetErr != nil {
		return nil, mb.cfg.PrepResetErr
	} else if mb.cfg.PrepResetRes == nil {
		return &storage.ScmPrepareResponse{}, nil
	}
	return mb.cfg.PrepResetRes, mb.cfg.PrepResetErr
}

func (mb *MockBackend) GetFirmwareStatus(deviceUID string) (*storage.ScmFirmwareInfo, error) {
	return mb.cfg.GetFirmwareStatusRes, mb.cfg.GetFirmwareStatusErr
}

func (mb *MockBackend) UpdateFirmware(deviceUID string, firmwarePath string) error {
	return mb.cfg.UpdateFirmwareErr
}

func NewMockBackend(cfg *MockBackendConfig) *MockBackend {
	if cfg == nil {
		cfg = &MockBackendConfig{}
	}
	return &MockBackend{
		cfg: *cfg,
	}
}

func DefaultMockBackend() *MockBackend {
	return NewMockBackend(nil)
}

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig, msc *system.MockSysConfig) *Provider {
	sysProv := system.NewMockSysProvider(log, msc)
	mountProv := mount.NewProvider(log, sysProv)
	return NewProvider(log, NewMockBackend(mbc), sysProv, mountProv)
}

func DefaultMockProvider(log logging.Logger) *Provider {
	sysProv := system.DefaultMockSysProvider(log)
	mountProv := mount.NewProvider(log, sysProv)
	return NewProvider(log, DefaultMockBackend(), sysProv, mountProv)
}
