//
// (C) Copyright 2019-2022 Intel Corporation.
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
	// GetfsUsageRetval encapsulates return values from a GetfsUsage call.
	GetfsUsageRetval struct {
		Total, Avail uint64
		Err          error
	}

	mountMap struct {
		sync.RWMutex
		mounted map[string]bool
	}

	// MockSysConfig alters mock SystemProvider behavior.
	MockSysConfig struct {
		IsMountedBool   bool
		IsMountedErr    error
		MountErr        error
		UnmountErr      error
		MkfsErr         error
		ChmodErr        error
		GetfsStr        string
		GetfsErr        error
		SourceToTarget  map[string]string
		getfsIndex      int
		GetfsUsageResps []GetfsUsageRetval
		statErrors      map[string]error
		realStat        bool
	}

	// MockSysProvider gives a mock SystemProvider implementation.
	MockSysProvider struct {
		sync.RWMutex
		log       logging.Logger
		cfg       MockSysConfig
		isMounted mountMap
	}
)

func (mm *mountMap) Set(mount string, mounted bool) {
	mm.Lock()
	defer mm.Unlock()

	mm.mounted[mount] = mounted
}

func (mm *mountMap) Get(mount string) (bool, bool) {
	mm.RLock()
	defer mm.RUnlock()

	mounted, exists := mm.mounted[mount]
	return mounted, exists
}

func (msp *MockSysProvider) IsMounted(target string) (bool, error) {
	err := msp.cfg.IsMountedErr
	// hack... don't fail the format tests which also want
	// to make sure that the device isn't already formatted.
	if os.IsNotExist(err) && strings.HasPrefix(target, "/dev") {
		err = nil
	}

	msp.Lock()
	defer msp.Unlock()

	// lookup target of a given source device (target actually a source
	// device in this case)
	mount, exists := msp.cfg.SourceToTarget[target]
	if exists {
		target = mount
	}

	isMounted, exists := msp.isMounted.Get(target)
	if !exists {
		return msp.cfg.IsMountedBool, err
	}
	return isMounted, err
}

func (msp *MockSysProvider) Mount(_, target, _ string, _ uintptr, _ string) error {
	if msp.cfg.MountErr == nil {
		msp.Lock()
		msp.isMounted.Set(target, true)
		msp.Unlock()
	}
	return msp.cfg.MountErr
}

func (msp *MockSysProvider) Unmount(target string, _ int) error {
	if msp.cfg.UnmountErr == nil {
		msp.Lock()
		msp.isMounted.Set(target, false)
		msp.Unlock()
	}
	return msp.cfg.UnmountErr
}

func (msp *MockSysProvider) Mkfs(_, _ string, _ bool) error {
	return msp.cfg.MkfsErr
}

func (msp *MockSysProvider) Chmod(string, os.FileMode) error {
	return msp.cfg.ChmodErr
}

func (msp *MockSysProvider) Getfs(_ string) (string, error) {
	return msp.cfg.GetfsStr, msp.cfg.GetfsErr
}

func (msp *MockSysProvider) GetfsUsage(_ string) (uint64, uint64, error) {
	msp.cfg.getfsIndex += 1
	if len(msp.cfg.GetfsUsageResps) < msp.cfg.getfsIndex {
		return 0, 0, nil
	}
	resp := msp.cfg.GetfsUsageResps[msp.cfg.getfsIndex-1]
	return resp.Total, resp.Avail, resp.Err
}

func (msp *MockSysProvider) Stat(path string) (os.FileInfo, error) {
	msp.RLock()
	defer msp.RUnlock()

	if msp.cfg.realStat {
		return os.Stat(path)
	}

	// default return value for missing key is nil so
	// add entries to indicate path failure e.g. perms or not-exist
	return nil, msp.cfg.statErrors[path]
}

func NewMockSysProvider(log logging.Logger, cfg *MockSysConfig) *MockSysProvider {
	if cfg == nil {
		cfg = &MockSysConfig{}
	}
	if cfg.statErrors == nil {
		cfg.realStat = true
	}
	msp := &MockSysProvider{
		log: log,
		cfg: *cfg,
		isMounted: mountMap{
			mounted: make(map[string]bool),
		},
	}
	log.Debugf("creating MockSysProvider with cfg: %+v", msp.cfg)
	return msp
}

func DefaultMockSysProvider(log logging.Logger) *MockSysProvider {
	return NewMockSysProvider(log, nil)
}

// MockBackendConfig specifies behavior for a mock SCM backend
// implementation providing capability to access and configure
// SCM modules and namespaces.
type MockBackendConfig struct {
	GetModulesRes        storage.ScmModules
	GetModulesErr        error
	GetNamespacesRes     storage.ScmNamespaces
	GetNamespacesErr     error
	GetStateRes          *storage.ScmSocketState
	GetStateErr          error
	PrepRes              *storage.ScmPrepareResponse
	PrepErr              error
	PrepResetRes         *storage.ScmPrepareResponse
	PrepResetErr         error
	GetFirmwareStatusErr error
	GetFirmwareStatusRes *storage.ScmFirmwareInfo
	UpdateFirmwareErr    error
}

type MockBackend struct {
	cfg MockBackendConfig
}

func (mb *MockBackend) getModules(int) (storage.ScmModules, error) {
	return mb.cfg.GetModulesRes, mb.cfg.GetModulesErr
}

func (mb *MockBackend) getNamespaces(int) (storage.ScmNamespaces, error) {
	return mb.cfg.GetNamespacesRes, mb.cfg.GetNamespacesErr
}

func (mb *MockBackend) getPMemState(int) (*storage.ScmSocketState, error) {
	return mb.cfg.GetStateRes, mb.cfg.GetStateErr
}

func (mb *MockBackend) prep(storage.ScmPrepareRequest, *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	return mb.cfg.PrepRes, mb.cfg.PrepErr
}

func (mb *MockBackend) prepReset(storage.ScmPrepareRequest, *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
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

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig, msc *MockSysConfig) *Provider {
	return NewProvider(log, NewMockBackend(mbc), NewMockSysProvider(log, msc))
}

func DefaultMockProvider(log logging.Logger) *Provider {
	return NewProvider(log, DefaultMockBackend(), DefaultMockSysProvider(log))
}
