//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"os"
	"strings"
	"sync"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	MockSysConfig struct {
		IsMountedBool   bool
		IsMountedErr    error
		MountErr        error
		UnmountErr      error
		MkfsErr         error
		GetfsStr        string
		GetfsErr        error
		SourceToTarget  map[string]string
		GetfsUsageTotal uint64
		GetfsUsageAvail uint64
		GetfsUsageErr   error
	}

	MockSysProvider struct {
		sync.RWMutex
		cfg       MockSysConfig
		isMounted map[string]bool
	}
)

func (msp *MockSysProvider) IsMounted(target string) (bool, error) {
	err := msp.cfg.IsMountedErr
	// hack... don't fail the format tests which also want
	// to make sure that the device isn't already formatted.
	if os.IsNotExist(err) && strings.HasPrefix(target, "/dev") {
		err = nil
	}

	// lookup target of a given source device (target actually a source
	// device in this case)
	mount, exists := msp.cfg.SourceToTarget[target]
	if exists {
		target = mount
	}

	msp.RLock()
	defer msp.RUnlock()
	isMounted, exists := msp.isMounted[target]
	if !exists {
		return msp.cfg.IsMountedBool, err
	}
	return isMounted, err
}

func (msp *MockSysProvider) Mount(_, target, _ string, _ uintptr, _ string) error {
	if msp.cfg.MountErr == nil {
		msp.Lock()
		defer msp.Unlock()
		msp.isMounted[target] = true
	}
	return msp.cfg.MountErr
}

func (msp *MockSysProvider) Unmount(target string, _ int) error {
	if msp.cfg.UnmountErr == nil {
		msp.Lock()
		defer msp.Unlock()
		msp.isMounted[target] = false
	}
	return msp.cfg.UnmountErr
}

func (msp *MockSysProvider) Mkfs(_, _ string, _ bool) error {
	return msp.cfg.MkfsErr
}

func (msp *MockSysProvider) Getfs(_ string) (string, error) {
	return msp.cfg.GetfsStr, msp.cfg.GetfsErr
}

func (msp *MockSysProvider) GetfsUsage(_ string) (uint64, uint64, error) {
	return msp.cfg.GetfsUsageTotal, msp.cfg.GetfsUsageAvail, msp.cfg.GetfsUsageErr
}

func NewMockSysProvider(cfg *MockSysConfig) *MockSysProvider {
	if cfg == nil {
		cfg = &MockSysConfig{}
	}
	return &MockSysProvider{
		cfg:       *cfg,
		isMounted: make(map[string]bool),
	}
}

func DefaultMockSysProvider() *MockSysProvider {
	return NewMockSysProvider(nil)
}

// MockBackendConfig specifies behavior for a mock SCM backend
// implementation providing capability to access and configure
// SCM modules and namespaces.
type MockBackendConfig struct {
	DiscoverRes          storage.ScmModules
	DiscoverErr          error
	GetPmemNamespaceRes  storage.ScmNamespaces
	GetPmemNamespaceErr  error
	GetPmemStateErr      error
	StartingState        storage.ScmState
	NextState            storage.ScmState
	PrepNeedsReboot      bool
	PrepNamespaceRes     storage.ScmNamespaces
	PrepErr              error
	GetFirmwareStatusErr error
	GetFirmwareStatusRes *storage.ScmFirmwareInfo
	UpdateFirmwareErr    error
}

type MockBackend struct {
	sync.RWMutex
	curState storage.ScmState
	cfg      MockBackendConfig
}

func (mb *MockBackend) Discover() (storage.ScmModules, error) {
	return mb.cfg.DiscoverRes, mb.cfg.DiscoverErr
}

func (mb *MockBackend) GetPmemNamespaces() (storage.ScmNamespaces, error) {
	return mb.cfg.GetPmemNamespaceRes, mb.cfg.GetPmemNamespaceErr
}

func (mb *MockBackend) GetPmemState() (storage.ScmState, error) {
	if mb.cfg.GetPmemStateErr != nil {
		return storage.ScmStateUnknown, mb.cfg.GetPmemStateErr
	}
	mb.RLock()
	defer mb.RUnlock()
	return mb.curState, nil
}

func (mb *MockBackend) Prep(_ storage.ScmState) (bool, storage.ScmNamespaces, error) {
	if mb.cfg.PrepErr == nil {
		mb.Lock()
		mb.curState = mb.cfg.NextState
		mb.Unlock()
	}
	return mb.cfg.PrepNeedsReboot, mb.cfg.PrepNamespaceRes, mb.cfg.PrepErr
}

func (mb *MockBackend) PrepReset(_ storage.ScmState) (bool, error) {
	if mb.cfg.PrepErr == nil {
		mb.Lock()
		mb.curState = mb.cfg.NextState
		mb.Unlock()
	}
	return mb.cfg.PrepNeedsReboot, mb.cfg.PrepErr
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
