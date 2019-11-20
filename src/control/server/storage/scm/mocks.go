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

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
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

// MockBackendConfig specifies behaviour for a mock SCM backend
// implementation providing capability to access and configure
// SCM modules and namespaces.
type MockBackendConfig struct {
	DiscoverRes      storage.ScmModules
	DiscoverErr      error
	GetNamespaceRes  storage.ScmNamespaces
	GetNamespaceErr  error
	GetStateErr      error
	StartingState    storage.ScmState
	NextState        storage.ScmState
	PrepNeedsReboot  bool
	PrepNamespaceRes storage.ScmNamespaces
	PrepErr          error
}

type MockBackend struct {
	curState storage.ScmState
	cfg      MockBackendConfig
}

func (mb *MockBackend) Discover() (storage.ScmModules, error) {
	return mb.cfg.DiscoverRes, mb.cfg.DiscoverErr
}

func (mb *MockBackend) GetNamespaces() (storage.ScmNamespaces, error) {
	return mb.cfg.GetNamespaceRes, mb.cfg.GetNamespaceErr
}

func (mb *MockBackend) GetState() (storage.ScmState, error) {
	if mb.cfg.GetStateErr != nil {
		return storage.ScmStateUnknown, mb.cfg.GetStateErr
	}
	return mb.curState, nil
}

func (mb *MockBackend) Prep(_ storage.ScmState) (bool, storage.ScmNamespaces, error) {
	if mb.cfg.PrepErr == nil {
		mb.curState = mb.cfg.NextState
	}
	return mb.cfg.PrepNeedsReboot, mb.cfg.PrepNamespaceRes, mb.cfg.PrepErr
}

func (mb *MockBackend) PrepReset(_ storage.ScmState) (bool, error) {
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

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig, msc *MockSysConfig) *Provider {
	return NewProvider(log, NewMockBackend(mbc), NewMockSysProvider(msc)).WithForwardingDisabled()
}

func DefaultMockProvider(log logging.Logger) *Provider {
	return NewProvider(log, DefaultMockBackend(), DefaultMockSysProvider()).WithForwardingDisabled()
}
