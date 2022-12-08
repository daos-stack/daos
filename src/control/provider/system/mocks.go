//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"os"
	"sync"

	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// GetfsUsageRetval encapsulates return values from a GetfsUsage call.
	GetfsUsageRetval struct {
		Total, Avail uint64
		Err          error
	}

	MountMap struct {
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
		ChownErr        error
		GetfsStr        string
		GetfsErr        error
		SourceToTarget  map[string]string
		GetfsIndex      int
		GetfsUsageResps []GetfsUsageRetval
		StatErrors      map[string]error
		RealStat        bool
	}

	// MockSysProvider gives a mock SystemProvider implementation.
	MockSysProvider struct {
		sync.RWMutex
		log       logging.Logger
		cfg       MockSysConfig
		isMounted MountMap
	}
)

func (mm *MountMap) Set(mount string, mounted bool) {
	mm.Lock()
	defer mm.Unlock()

	mm.mounted[mount] = mounted
}

func (mm *MountMap) Get(mount string) (bool, bool) {
	mm.RLock()
	defer mm.RUnlock()

	mounted, exists := mm.mounted[mount]
	return mounted, exists
}

func (msp *MockSysProvider) IsMounted(target string) (bool, error) {
	err := msp.cfg.IsMountedErr

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

func (msp *MockSysProvider) Chown(string, int, int) error {
	return msp.cfg.ChownErr
}

func (msp *MockSysProvider) Getfs(_ string) (string, error) {
	return msp.cfg.GetfsStr, msp.cfg.GetfsErr
}

func (msp *MockSysProvider) GetfsUsage(_ string) (uint64, uint64, error) {
	msp.cfg.GetfsIndex += 1
	if len(msp.cfg.GetfsUsageResps) < msp.cfg.GetfsIndex {
		return 0, 0, nil
	}
	resp := msp.cfg.GetfsUsageResps[msp.cfg.GetfsIndex-1]
	return resp.Total, resp.Avail, resp.Err
}

func (msp *MockSysProvider) Stat(path string) (os.FileInfo, error) {
	msp.RLock()
	defer msp.RUnlock()

	if msp.cfg.RealStat {
		return os.Stat(path)
	}

	// default return value for missing key is nil so
	// add entries to indicate path failure e.g. perms or not-exist
	return nil, msp.cfg.StatErrors[path]
}

func NewMockSysProvider(log logging.Logger, cfg *MockSysConfig) *MockSysProvider {
	if cfg == nil {
		cfg = &MockSysConfig{}
	}
	if cfg.StatErrors == nil {
		cfg.RealStat = true
	}
	msp := &MockSysProvider{
		log: log,
		cfg: *cfg,
		isMounted: MountMap{
			mounted: make(map[string]bool),
		},
	}
	log.Debugf("creating MockSysProvider with cfg: %+v", msp.cfg)
	return msp
}

func DefaultMockSysProvider(log logging.Logger) *MockSysProvider {
	return NewMockSysProvider(log, nil)
}
