//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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

// findBdevsWithDomain retrieves controllers in scan response that match the
// input prefix in the domain component of their PCI address.
func findBdevsWithDomain(scanResp *bdev.ScanResponse, prefix string) ([]string, error) {
	var found []string

	for _, ctrlr := range scanResp.Controllers {
		domain, _, _, _, err := common.ParsePCIAddress(ctrlr.PciAddr)
		if err != nil {
			return nil, err
		}
		if fmt.Sprintf("%x", domain) == prefix {
			found = append(found, ctrlr.PciAddr)
		}
	}

	return found, nil
}

// substBdevVmdAddrs replaces VMD PCI addresses in bdev device list with the
// PCI addresses of the backing devices behind the VMD.
//
// Select any PCI addresses that have the compressed VMD address BDF as backing
// address domain.
//
// Return new device list with PCI addresses of devices behind the VMD.
func substBdevVmdAddrs(cfgBdevs []string, scanResp *bdev.ScanResponse) ([]string, error) {
	if len(scanResp.Controllers) == 0 {
		return nil, nil
	}

	var newCfgBdevs []string
	for _, dev := range cfgBdevs {
		_, b, d, f, err := common.ParsePCIAddress(dev)
		if err != nil {
			return nil, err
		}
		matchDevs, err := findBdevsWithDomain(scanResp,
			fmt.Sprintf("%02x%02x%02x", b, d, f))
		if err != nil {
			return nil, err
		}
		if len(matchDevs) == 0 {
			newCfgBdevs = append(newCfgBdevs, dev)
			continue
		}
		newCfgBdevs = append(newCfgBdevs, matchDevs...)
	}

	return newCfgBdevs, nil
}

// canAccessBdevs evaluates if any specified Bdevs are not accessible.
//
// Specified Bdevs can be VMD addresses.
//
// Return any device addresses missing from the scan response and ok set to true
// if no devices are missing.
func canAccessBdevs(cfgBdevs []string, scanResp *bdev.ScanResponse) ([]string, bool) {
	var missing []string

	for _, pciAddr := range cfgBdevs {
		var found bool

		for _, ctrlr := range scanResp.Controllers {
			if ctrlr.PciAddr == pciAddr {
				found = true
				break
			}
		}
		if !found {
			missing = append(missing, pciAddr)
		}
	}

	return missing, len(missing) == 0
}

// checkCfgBdevs performs validation on NVMe returned from initial scan.
func (c *StorageControlService) checkCfgBdevs(scanResp *bdev.ScanResponse) error {
	if scanResp == nil {
		return errors.New("received nil scan response")
	}
	if len(c.instanceStorage) == 0 {
		return nil
	}

	for idx, storageCfg := range c.instanceStorage {
		cfgBdevs := storageCfg.Bdev.GetNvmeDevs()
		if len(cfgBdevs) == 0 {
			continue
		}

		if !c.bdev.IsVMDDisabled() {
			c.log.Debug("VMD detected, processing PCI addresses")
			newBdevs, err := substBdevVmdAddrs(cfgBdevs, scanResp)
			if err != nil {
				return err
			}
			if len(newBdevs) == 0 {
				return errors.New("unexpected empty bdev list returned " +
					"check vmd address has backing devices")
			}
			c.log.Debugf("instance %d: subst vmd addrs %v->%v",
				idx, cfgBdevs, newBdevs)
			cfgBdevs = newBdevs
			c.instanceStorage[idx].Bdev.DeviceList = cfgBdevs
		}

		// fail if config specified nvme devices are inaccessible
		missing, ok := canAccessBdevs(cfgBdevs, scanResp)
		if !ok {
			return FaultBdevNotFound(missing)
		}
	}

	return nil
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

	nvmeScanResp, err := c.NvmeScan(bdev.ScanRequest{})
	if err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, NVMe Scan"))
		return nil
	}

	if err := c.checkCfgBdevs(nvmeScanResp); err != nil {
		return errors.Wrap(err, "validate server config bdevs")
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
	return c.scm.GetPmemState()
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
