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

package server

import (
	"os"

	"github.com/pkg/errors"

	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// StorageControlService encapsulates the storage part of the control service
type StorageControlService struct {
	log             logging.Logger
	ext             External
	nvme            *nvmeStorage
	scm             *scm.Provider
	instanceStorage []ioserver.StorageConfig
}

// DefaultStorageControlService returns a initialized *StorageControlService
// with default behaviour
func DefaultStorageControlService(log logging.Logger, cfg *Configuration) (*StorageControlService, error) {
	scriptPath, err := cfg.ext.getAbsInstallPath(spdkSetupPath)
	if err != nil {
		return nil, err
	}

	spdkScript := &spdkSetup{
		log:         log,
		scriptPath:  scriptPath,
		nrHugePages: cfg.NrHugepages,
	}

	return NewStorageControlService(log, cfg.ext,
		newNvmeStorage(log, cfg.NvmeShmID, spdkScript, cfg.ext),
		scm.DefaultProvider(log), cfg.Servers), nil
}

// NewStorageControlService returns an initialized *StorageControlService
func NewStorageControlService(log logging.Logger, ext External, nvme *nvmeStorage, scm *scm.Provider, srvCfgs []*ioserver.Config) *StorageControlService {
	instanceStorage := []ioserver.StorageConfig{}
	for _, srvCfg := range srvCfgs {
		instanceStorage = append(instanceStorage, srvCfg.Storage)
	}

	return &StorageControlService{
		log:             log,
		ext:             ext,
		nvme:            nvme,
		scm:             scm,
		instanceStorage: instanceStorage,
	}
}

// canAccessBdevs evaluates if any specified Bdevs are not accessible.
func (c *StorageControlService) canAccessBdevs() (missing []string, ok bool) {
	for _, storageCfg := range c.instanceStorage {
		_missing, _ok := c.nvme.hasControllers(storageCfg.Bdev.GetNvmeDevs())
		if !_ok {
			missing = append(missing, _missing...)
		}
	}

	return missing, len(missing) == 0
}

// Setup delegates to Storage implementation's Setup methods.
func (c *StorageControlService) Setup() error {
	if err := c.nvme.Setup(); err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, NVMe Setup"))
	}

	// fail if config specified nvme devices are inaccessible
	missing, ok := c.canAccessBdevs()
	if !ok {
		return errors.Errorf("%s: missing %v", msgBdevNotFound, missing)
	}

	if _, err := c.scm.Scan(scm.ScanRequest{}); err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, SCM Scan"))
	}

	return nil
}

// Teardown delegates to Storage implementation's Teardown methods.
func (c *StorageControlService) Teardown() {
	if err := c.nvme.Teardown(); err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, NVMe Teardown"))
	}

	// TODO: implement provider Reset()
	//c.scm.scanCompleted = false
}

// NvmePrepareRequest encapsulates request parameters for operation.
type NvmePrepareRequest struct {
	HugePageCount int
	TargetUser    string
	PCIWhitelist  string
	ResetOnly     bool
}

// NvmePrepare preps locally attached SSDs and returns error.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) NvmePrepare(req NvmePrepareRequest) error {
	ok, usr := c.nvme.ext.checkSudo()
	if !ok {
		return errors.Errorf("%s must be run as root or sudo in order to prepare NVMe in this release", os.Args[0])
	}

	// falls back to sudoer or root if TargetUser is unspecified
	tUsr := usr
	if req.TargetUser != "" {
		tUsr = req.TargetUser
	}

	// run reset first to ensure reallocation of hugepages
	if err := c.nvme.spdk.reset(); err != nil {
		return errors.WithMessage(err, "SPDK setup reset")
	}

	// if we're only resetting, just return before prep
	if req.ResetOnly {
		return nil
	}

	return errors.WithMessage(
		c.nvme.spdk.prep(req.HugePageCount, tUsr, req.PCIWhitelist),
		"SPDK setup",
	)
}

// GetScmState performs required initialisation and returns current state
// of SCM module preparation.
func (c *StorageControlService) GetScmState() (storage.ScmState, error) {
	return c.scm.GetState()
}

// ScmPrepare preps locally attached modules and returns need to reboot message,
// list of pmem device files and error directly.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) ScmPrepare(req scm.PrepareRequest) (*scm.PrepareResponse, error) {
	// transition to the next state in SCM preparation
	return c.scm.Prepare(req)
}

// NvmeScan scans locally attached SSDs and returns list directly.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) NvmeScan() (types.NvmeControllers, error) {
	if err := c.nvme.Discover(); err != nil {
		return nil, errors.Wrap(err, "NVMe storage scan")
	}

	return c.nvme.controllers, nil
}

// ScmScan scans locally attached modules, namespaces and state of DCPM config.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) ScmScan() (*scm.ScanResponse, error) {
	return c.scm.Scan(scm.ScanRequest{})
}
