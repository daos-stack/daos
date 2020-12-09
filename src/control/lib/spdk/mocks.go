//
// (C) Copyright 2018-2020 Intel Corporation.
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

package spdk

import (
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// MockEnvCfg controls the behavior of the MockEnvImpl.
type MockEnvCfg struct {
	InitErr error
}

// MockEnvImpl is a mock implementation of the Env interface.
type MockEnvImpl struct {
	Cfg MockEnvCfg
}

// InitSPDKEnv initializes the SPDK environment.
func (e *MockEnvImpl) InitSPDKEnv(log logging.Logger, opts *EnvOptions) error {
	if e.Cfg.InitErr == nil {
		log.Debugf("mock spdk init go opts: %+v", opts)
	}
	return e.Cfg.InitErr
}

// FiniSPDKEnv finalizes the SPDK environment.
func (e *MockEnvImpl) FiniSPDKEnv(log logging.Logger, opts *EnvOptions) {
}

// MockNvmeCfg controls the behavior of the MockNvmeImpl.
type MockNvmeCfg struct {
	DiscoverCtrlrs storage.NvmeControllers
	DiscoverErr    error
	FormatRes      []*FormatResult
	FormatErr      error
	UpdateErr      error
}

// MockNvmeImpl is an implementation of the Nvme interface.
type MockNvmeImpl struct {
	Cfg MockNvmeCfg
}

// CleanLockfiles removes SPDK lockfiles after binding operations.
func (n *MockNvmeImpl) CleanLockfiles(log logging.Logger, pciAddrs ...string) error {
	log.Debugf("mock clean lockfiles pci addresses: %v", pciAddrs)

	return nil
}

// Discover NVMe devices, including NVMe devices behind VMDs if enabled,
// accessible by SPDK on a given host.
func (n *MockNvmeImpl) Discover(log logging.Logger) (storage.NvmeControllers, error) {
	if n.Cfg.DiscoverErr != nil {
		return nil, n.Cfg.DiscoverErr
	}
	log.Debugf("mock discover nvme ssds: %v", n.Cfg.DiscoverCtrlrs)

	return n.Cfg.DiscoverCtrlrs, nil
}

// Format device at given pci address, destructive operation!
func (n *MockNvmeImpl) Format(log logging.Logger) ([]*FormatResult, error) {
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
func (n *MockNvmeImpl) Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error {
	if n.Cfg.UpdateErr != nil {
		return n.Cfg.UpdateErr
	}
	log.Debugf("mock update fw on nvme ssd: %q, image path %q, slot %d",
		ctrlrPciAddr, path, slot)

	return nil
}
