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
package scm

import (
	"os"
	"strings"

	types "github.com/daos-stack/daos/src/control/common/storage"
)

type (
	MockSysConfig struct {
		IsMountedBool bool
		IsMountedErr  error
		MountErr      error
		UnmountErr    error
		MkfsErr       error
		GetfsStr      string
		GetfsErr      error
	}

	MockSysProvider struct {
		cfg MockSysConfig
	}
)

// MockDiscovery returns a mock SCM module of type exported from ipmctl.
func MockDiscovery() ipmctl.DeviceDiscovery {
	m := MockModulePB()

	return ipmctl.DeviceDiscovery{
		Physical_id:          uint16(m.Physicalid),
		Channel_id:           uint16(m.Loc.Channel),
		Channel_pos:          uint16(m.Loc.Channelpos),
		Memory_controller_id: uint16(m.Loc.Memctrlr),
		Socket_id:            uint16(m.Loc.Socket),
		Capacity:             m.Capacity,
	}
}

type mockIpmctl struct {
	discoverModulesRet error
	modules            []ipmctl.DeviceDiscovery
}

func (m *mockIpmctl) Discover() ([]ipmctl.DeviceDiscovery, error) {
	if m.discoverModulesRet != nil {
		return nil, m.discoverModulesRet
	}
	return m.modules, nil
}

type mockCmdRunner struct {
	binding ipmctl.Ipmctl
}

func (msp *MockSysProvider) IsMounted(target string) (bool, error) {
	// hack... don't fail the format tests which also want
	// to make sure that the device isn't already formatted.
	if os.IsNotExist(msp.cfg.IsMountedErr) && strings.HasPrefix(target, "/dev") {
		return msp.cfg.IsMountedBool, nil
	}
	return msp.cfg.IsMountedBool, msp.cfg.IsMountedErr
}

func (msp *MockSysProvider) Mount(_, _, _ string, _ uintptr, _ string) error {
	if msp.cfg.MountErr == nil {
		msp.cfg.IsMountedBool = true
	}
	return msp.cfg.MountErr
}

func (msp *MockSysProvider) Unmount(_ string, _ int) error {
	if msp.cfg.UnmountErr == nil {
		msp.cfg.IsMountedBool = false
	}
	return msp.cfg.UnmountErr
}

func (msp *MockSysProvider) Mkfs(_, _ string, _ bool) error {
	return msp.cfg.MkfsErr
}

func (msp *MockSysProvider) Getfs(_ string) (string, error) {
	return msp.cfg.GetfsStr, msp.cfg.GetfsErr
}

func NewMockSysProvider(cfg *MockSysConfig) *MockSysProvider {
	if cfg == nil {
		cfg = &MockSysConfig{}
	}
	return &MockSysProvider{
		cfg: *cfg,
	}
}

func DefaultMockSysProvider() *MockSysProvider {
	return NewMockSysProvider(nil)
}

type MockBackendConfig struct {
	GetModulesRes    []Module
	GetModulesErr    error
	GetNamespaceRes  []Namespace
	GetNamespaceErr  error
	GetStateErr      error
	StartingState    types.ScmState
	NextState        types.ScmState
	PrepNeedsReboot  bool
	PrepNamespaceRes []Namespace
	PrepErr          error
}

type MockBackend struct {
	curState types.ScmState
	cfg      MockBackendConfig
}

func (mb *MockBackend) GetModules() ([]Module, error) {
	return mb.cfg.GetModulesRes, mb.cfg.GetModulesErr
}

func (mb *MockBackend) GetNamespaces() ([]Namespace, error) {
	return mb.cfg.GetNamespaceRes, mb.cfg.GetNamespaceErr
}

func (mb *MockBackend) GetState() (types.ScmState, error) {
	if mb.cfg.GetStateErr != nil {
		return types.ScmStateUnknown, mb.cfg.GetStateErr
	}
	return mb.curState, nil
}

func (mb *MockBackend) Prep(_ types.ScmState) (bool, []Namespace, error) {
	if mb.cfg.PrepErr == nil {
		mb.curState = mb.cfg.NextState
	}
	return mb.cfg.PrepNeedsReboot, mb.cfg.PrepNamespaceRes, mb.cfg.PrepErr
}

func (mb *MockBackend) PrepReset(_ types.ScmState) (bool, error) {
	if mb.cfg.PrepErr == nil {
		mb.curState = mb.cfg.NextState
	}
	return mb.cfg.PrepNeedsReboot, mb.cfg.PrepErr
}

func NewMockBackend(cfg *MockBackendConfig) *MockBackend {
	if cfg == nil {
		cfg = &MockBackendConfig{}
	}
	return &MockBackend{
		curState: cfg.StartingState,
		cfg:      *cfg,
	}
}

func DefaultMockBackend() *MockBackend {
	return NewMockBackend(nil)
}
