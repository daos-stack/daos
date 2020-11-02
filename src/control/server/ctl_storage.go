//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"fmt"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// StorageControlService encapsulates the storage part of the control service
type StorageControlService struct {
	log             logging.Logger
	bdev            *bdev.Provider
	scm             *scm.Provider
	instanceStorage []*ioserver.StorageConfig
}

// NewStorageControlService returns an initialized *StorageControlService
func NewStorageControlService(log logging.Logger, bdev *bdev.Provider, scm *scm.Provider, srvCfgs []*ioserver.Config) *StorageControlService {
	instanceStorage := []*ioserver.StorageConfig{}
	for _, srvCfg := range srvCfgs {
		instanceStorage = append(instanceStorage, &srvCfg.Storage)
	}

	return &StorageControlService{
		log:             log,
		bdev:            bdev,
		scm:             scm,
		instanceStorage: instanceStorage,
	}
}

func (c *StorageControlService) findBdevsWithDomain(sr *bdev.ScanResponse, prefix string) ([]string, error) {
	var found []string

	for _, c := range sr.Controllers {
		domain, _, _, _, err := bdev.ParsePCIAddress(c.PciAddr)
		if err != nil {
			return nil, err
		}
		if fmt.Sprintf("%x", domain) == prefix {
			found = append(found, c.PciAddr)
		}
	}

	return found, nil
}

// substBdevVmdAddrs replaces VMD PCI addresses in bdev device list with the
// PCI addresses of the backing devices behind the VMD.
//
// Select any PCI addresses that have the compressed VMD address BDF as backing
// address domain.
func (c *StorageControlService) substBdevVmdAddrs(sr *bdev.ScanResponse) error {
	if len(c.instanceStorage) == 0 {
		return nil
	}

	for i := range c.instanceStorage {
		var newDevs []string
		oldDevs := c.instanceStorage[i].Bdev.GetNvmeDevs()
		if len(oldDevs) == 0 {
			continue
		}

		for _, dev := range oldDevs {
			_, b, d, f, err := bdev.ParsePCIAddress(dev)
			if err != nil {
				return err
			}
			matchDevs, err := c.findBdevsWithDomain(sr,
				fmt.Sprintf("%02x%02x%02x", b, d, f))
			if err != nil {
				return err
			}
			if len(matchDevs) == 0 {
				matchDevs = append(matchDevs, dev)
			}
			newDevs = append(newDevs, matchDevs...)
		}

		c.instanceStorage[i].Bdev.DeviceList = newDevs
	}

	return nil
}

// canAccessBdevs evaluates if any specified Bdevs are not accessible.
//
// Specified Bdevs can be VMD addresses.
func (c *StorageControlService) canAccessBdevs(sr *bdev.ScanResponse) (missing []string, ok bool) {
	getController := func(pciAddr string) *storage.NvmeController {
		for _, c := range sr.Controllers {
			if c.PciAddr == pciAddr {
				return c
			}
		}
		return nil
	}

	for _, storageCfg := range c.instanceStorage {
		for _, pciAddr := range storageCfg.Bdev.GetNvmeDevs() {
			if getController(pciAddr) == nil {
				missing = append(missing, pciAddr)
			}
		}
	}

	return missing, len(missing) == 0
}

// Setup delegates to Storage implementation's Setup methods.
func (c *StorageControlService) Setup() error {
	if _, err := c.ScmScan(scm.ScanRequest{}); err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, SCM Scan"))
	}

	// don't scan if using emulated NVMe
	for _, storageCfg := range c.instanceStorage {
		if storageCfg.Bdev.Class != storage.BdevClassNvme {
			return nil
		}
	}

	sr, err := c.NvmeScan(bdev.ScanRequest{})
	if err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, NVMe Scan"))
		return nil
	}

	if !c.bdev.IsVMDDisabled() {
		if err := c.substBdevVmdAddrs(sr); err != nil {
			return err
		}
	}

	// fail if config specified nvme devices are inaccessible
	missing, ok := c.canAccessBdevs(sr)
	if !ok {
		return FaultBdevNotFound(missing)
	}

	return nil
}

// NvmePrepare preps locally attached SSDs and returns error.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) NvmePrepare(req bdev.PrepareRequest) (*bdev.PrepareResponse, error) {
	return c.bdev.Prepare(req)
}

// GetScmState performs required initialization and returns current state
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

// NvmeScan scans locally attached SSDs.
func (c *StorageControlService) NvmeScan(req bdev.ScanRequest) (*bdev.ScanResponse, error) {
	return c.bdev.Scan(req)
}

// ScmScan scans locally attached modules, namespaces and state of DCPM config.
func (c *StorageControlService) ScmScan(req scm.ScanRequest) (*scm.ScanResponse, error) {
	return c.scm.Scan(req)
}
