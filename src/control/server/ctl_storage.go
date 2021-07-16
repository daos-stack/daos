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
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// StorageControlService encapsulates the storage part of the control service
type StorageControlService struct {
	log             logging.Logger
	storage         *storage.Provider
	instanceStorage map[uint32]*storage.Config
}

// NewStorageControlService returns an initialized *StorageControlService
func NewStorageControlService(log logging.Logger, engineCfgs []*engine.Config) *StorageControlService {
	instanceStorage := make(map[uint32]*storage.Config)
	for i, cfg := range engineCfgs {
		instanceStorage[uint32(i)] = &cfg.Storage
	}

	return &StorageControlService{
		log: log,
		storage: storage.DefaultProvider(log, 0, &storage.Config{
			Tiers: nil,
		}),
		instanceStorage: instanceStorage,
	}
}

func NewMockStorageControlService(log logging.Logger, engineCfgs []*engine.Config, sys storage.SystemProvider, scm storage.ScmProvider, bdev storage.BdevProvider) *StorageControlService {
	instanceStorage := make(map[uint32]*storage.Config)
	for i, cfg := range engineCfgs {
		instanceStorage[uint32(i)] = &cfg.Storage
	}

	return &StorageControlService{
		log: log,
		storage: storage.MockProvider(log, 0, &storage.Config{
			Tiers: nil,
		}, sys, scm, bdev),
		instanceStorage: instanceStorage,
	}
}

// findBdevsWithDomain retrieves controllers in scan response that match the
// input prefix in the domain component of their PCI address.
func findBdevsWithDomain(scanResp *storage.BdevScanResponse, prefix string) ([]string, error) {
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
func substBdevVmdAddrs(cfgBdevs []string, scanResp *storage.BdevScanResponse) ([]string, error) {
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
func canAccessBdevs(cfgBdevs []string, scanResp *storage.BdevScanResponse) ([]string, bool) {
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
func (c *StorageControlService) checkCfgBdevs(scanResp *storage.BdevScanResponse) error {
	if scanResp == nil {
		return errors.New("received nil scan response")
	}
	if len(c.instanceStorage) == 0 {
		return nil
	}

	for _, storageCfg := range c.instanceStorage {
		for _, tierCfg := range storageCfg.Tiers {
			if !tierCfg.IsBdev() || len(tierCfg.Bdev.DeviceList) == 0 {
				continue
			}
			cfgBdevs := tierCfg.Bdev.DeviceList

			// TODO DAOS-8040: re-enable VMD
			// if !c.bdev.IsVMDDisabled() {
			// 	c.log.Debug("VMD detected, processing PCI addresses")
			// 	newBdevs, err := substBdevVmdAddrs(cfgBdevs, scanResp)
			// 	if err != nil {
			// 		return err
			// 	}
			// 	if len(newBdevs) == 0 {
			// 		return errors.New("unexpected empty bdev list returned " +
			// 			"check vmd address has backing devices")
			// 	}
			// 	c.log.Debugf("instance %d: subst vmd addrs %v->%v",
			// 		idx, cfgBdevs, newBdevs)
			// 	cfgBdevs = newBdevs
			// 	c.instanceStorage[idx].Bdev.DeviceList = cfgBdevs
			// }

			// fail if config specified nvme devices are inaccessible
			missing, ok := canAccessBdevs(cfgBdevs, scanResp)
			if !ok {
				return FaultBdevNotFound(missing)
			}
		}
	}

	return nil
}

// Setup delegates to Storage implementation's Setup methods.
func (c *StorageControlService) Setup() error {
	if _, err := c.ScmScan(storage.ScmScanRequest{}); err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, SCM Scan"))
	}

	// don't scan if using emulated NVMe
	for _, storageCfg := range c.instanceStorage {
		for _, tierCfg := range storageCfg.Tiers {
			if tierCfg.Class != storage.ClassNvme {
				return nil
			}
		}
	}

	nvmeScanResp, err := c.storage.ScanBdevs(storage.BdevScanRequest{
		BypassCache: true,
	})
	if err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, NVMe Scan"))
		return nil
	}

	if err := c.checkCfgBdevs(nvmeScanResp); err != nil {
		return errors.Wrap(err, "validate server config bdevs")
	}

	c.storage.SetBdevCache(*nvmeScanResp)
	return nil
}

// GetNvmeCache ...
func (c *StorageControlService) GetNvmeCache() (*storage.BdevScanResponse, error) {
	return c.storage.ScanBdevs(storage.BdevScanRequest{
		BypassCache: false,
	})
}

func (c *StorageControlService) defaultProvider() *storage.Provider {
	return storage.DefaultProvider(c.log, 0, &storage.Config{
		Tiers: nil,
	})
}

// NvmePrepare preps locally attached SSDs and returns error.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) NvmePrepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	return c.storage.PrepareBdevs(req)
}

// GetScmState performs required initialization and returns current state
// of SCM module preparation.
func (c *StorageControlService) GetScmState() (storage.ScmState, error) {
	return c.storage.Scm.GetPmemState()
}

// ScmPrepare preps locally attached modules and returns need to reboot message,
// list of pmem device files and error directly.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (c *StorageControlService) ScmPrepare(req storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error) {
	// transition to the next state in SCM preparation
	return c.storage.Scm.Prepare(req)
}

// NvmeScan scans locally attached SSDs and bypasses cache.
func (c *StorageControlService) NvmeScan(req storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
	req.BypassCache = true
	return c.storage.ScanBdevs(req)
}

// ScmScan scans locally attached modules, namespaces and state of DCPM config.
func (c *StorageControlService) ScmScan(req storage.ScmScanRequest) (*storage.ScmScanResponse, error) {
	return c.storage.Scm.Scan(req)
}
