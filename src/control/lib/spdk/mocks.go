//
// (C) Copyright 2018-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package spdk

import (
	"sync"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// MockEnvCfg controls the behavior of the MockEnvImpl.
type MockEnvCfg struct {
	InitErr error
}

// MockEnvImpl is a mock implementation of the Env interface.
type MockEnvImpl struct {
	sync.RWMutex
	Cfg       MockEnvCfg
	InitCalls []*EnvOptions
	FiniCalls []*EnvOptions
}

// InitSPDKEnv initializes the SPDK environment.
func (e *MockEnvImpl) InitSPDKEnv(log logging.Logger, opts *EnvOptions) error {
	if e.Cfg.InitErr == nil {
		log.Debugf("mock spdk init go opts: %+v", opts)
	}

	e.Lock()
	e.InitCalls = append(e.InitCalls, opts)
	e.Unlock()

	return e.Cfg.InitErr
}

// FiniSPDKEnv finalizes the SPDK environment.
func (e *MockEnvImpl) FiniSPDKEnv(log logging.Logger, opts *EnvOptions) {
	log.Debugf("mock spdk fini go opts: %+v", opts)

	e.Lock()
	e.FiniCalls = append(e.FiniCalls, opts)
	e.Unlock()
}

// MockNvmeCfg controls the behavior of the MockNvmeImpl.
type MockNvmeCfg struct {
	DiscoverCtrlrs  storage.NvmeControllers
	DiscoverErr     error
	FormatRes       []*FormatResult
	FormatErr       error
	UpdateErr       error
	LockfileDir     string
	RemoveFn        removeFn
	RemoveErr       error
	PciAddrCheckMap map[string]bool
	PciAddrCheckErr error
}

// MockNvmeImpl is an implementation of the Nvme interface.
type MockNvmeImpl struct {
	Cfg MockNvmeCfg
}

// Discover NVMe devices, including NVMe devices behind VMDs if enabled,
// accessible by SPDK on a given host.
func (n MockNvmeImpl) Discover(log logging.Logger) (storage.NvmeControllers, error) {
	if n.Cfg.DiscoverErr != nil {
		return nil, n.Cfg.DiscoverErr
	}
	log.Debugf("mock discover nvme ssds: %v", n.Cfg.DiscoverCtrlrs)

	return n.Cfg.DiscoverCtrlrs, nil
}

// Format device at given pci address, destructive operation!
func (n MockNvmeImpl) Format(log logging.Logger) ([]*FormatResult, error) {
	log.Debug("mock format nvme ssds")

	if n.Cfg.FormatErr != nil {
		return nil, n.Cfg.FormatErr
	}

	if n.Cfg.FormatRes == nil {
		return make([]*FormatResult, 0), nil
	}

	return n.Cfg.FormatRes, nil
}

// Update calls C.nvme_fwupdate to update controller firmware image.
func (n MockNvmeImpl) Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error {
	if n.Cfg.UpdateErr != nil {
		return n.Cfg.UpdateErr
	}
	log.Debugf("mock update fw on nvme ssd: %q, image path %q, slot %d",
		ctrlrPciAddr, path, slot)

	return nil
}

// Clean removes SPDK lockfiles associated with NVMe SSDs/controllers at given PCI addresses.
// Mock avoid making any changes to the filesystem by mocking remove function.
func (n MockNvmeImpl) Clean(pciAddrChecker LockfileAddrCheckFn) ([]string, error) {
	mockLocksRemove := func(fName string) error {
		if n.Cfg.RemoveFn == nil {
			return n.Cfg.RemoveErr
		}
		return n.Cfg.RemoveFn(fName)
	}

	mockPciAddrChecker := func(a string) (bool, error) {
		if pciAddrChecker != nil {
			return pciAddrChecker(a)
		}
		if n.Cfg.PciAddrCheckMap == nil {
			n.Cfg.PciAddrCheckMap = make(map[string]bool)
		}
		return n.Cfg.PciAddrCheckMap[a], n.Cfg.PciAddrCheckErr
	}

	return cleanLockfiles(n.Cfg.LockfileDir, mockPciAddrChecker, mockLocksRemove)
}
