//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"path/filepath"
	"strings"

	"github.com/dustin/go-humanize"
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

func newStorageControlService(l logging.Logger, ecs []*engine.Config, sp *storage.Provider) *StorageControlService {
	instanceStorage := make(map[uint32]*storage.Config)
	for i, c := range ecs {
		instanceStorage[uint32(i)] = &c.Storage
	}

	return &StorageControlService{
		log:             l,
		storage:         sp,
		instanceStorage: instanceStorage,
	}
}

// NewStorageControlService returns an initialized *StorageControlService
func NewStorageControlService(log logging.Logger, engineCfgs []*engine.Config) *StorageControlService {
	return newStorageControlService(log, engineCfgs,
		storage.DefaultProvider(log, 0, &storage.Config{
			Tiers: nil,
		}),
	)
}

// NewMockStorageControlService returns a StorageControlService with a mocked
// storage provider consisting of the given sys, scm and bdev providers.
func NewMockStorageControlService(log logging.Logger, engineCfgs []*engine.Config, sys storage.SystemProvider, scm storage.ScmProvider, bdev storage.BdevProvider) *StorageControlService {
	return newStorageControlService(log, engineCfgs,
		storage.MockProvider(log, 0, &storage.Config{
			Tiers: nil,
		}, sys, scm, bdev),
	)
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
	if scanResp == nil || scanResp.Controllers == nil {
		return errors.New("received nil scan response or controllers")
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
	// TODO: this should probably be verified and scan per engine
	for _, storageCfg := range c.instanceStorage {
		for _, tierCfg := range storageCfg.Tiers.BdevConfigs() {
			if tierCfg.Class != storage.ClassNvme {
				return nil
			}
		}
	}

	nvmeScanResp, err := c.NvmeScan(storage.BdevScanRequest{})
	if err != nil {
		c.log.Debugf("%s\n", errors.Wrap(err, "Warning, NVMe Scan"))
		return nil
	}

	if err := c.checkCfgBdevs(nvmeScanResp); err != nil {
		return errors.Wrap(err, "validate server config bdevs")
	}

	c.storage.SetBdevCache(*nvmeScanResp)
	c.log.Debugf("set bdev cache after initial scan on start-up: %v",
		nvmeScanResp.Controllers)

	return nil
}

func (c *StorageControlService) defaultProvider() *storage.Provider {
	return storage.DefaultProvider(c.log, 0, &storage.Config{
		Tiers: nil,
	})
}

func findPMemInScan(ssr *storage.ScmScanResponse, pmemDevs []string) *storage.ScmNamespace {
	for _, scanned := range ssr.Namespaces {
		for _, path := range pmemDevs {
			if strings.TrimSpace(path) == "" {
				continue
			}
			if filepath.Base(path) == scanned.BlockDevice {
				return scanned
			}
		}
	}

	return nil
}

// getScmUsage will retrieve usage statistics (how much space is available for
// new DAOS pools) for either PMem namespaces or SCM emulation with ramdisk.
//
// Usage is only retrieved for active mountpoints being used by online DAOS I/O
// Server instances.
func (c *ControlService) getScmUsage(ssr *storage.ScmScanResponse) (*storage.ScmScanResponse, error) {
	if ssr == nil {
		return nil, errors.New("input scm scan response is nil")
	}

	instances := c.harness.Instances()

	nss := make(storage.ScmNamespaces, len(instances))
	for idx, ei := range instances {
		if !ei.IsReady() {
			continue // skip if not running
		}

		cfg, err := ei.GetScmConfig()
		if err != nil {
			return nil, err
		}

		mount, err := ei.GetScmUsage()
		if err != nil {
			return nil, err
		}

		switch mount.Class {
		case storage.ClassRam: // generate fake namespace for emulated ramdisk mounts
			nss[idx] = &storage.ScmNamespace{
				Mount:       mount,
				BlockDevice: "ramdisk",
				Size:        uint64(humanize.GiByte * cfg.Scm.RamdiskSize),
			}
		case storage.ClassDcpm: // update namespace mount info for online storage
			if ssr.Namespaces == nil {
				return nil, errors.Errorf("instance %d: input scm scan response missing namespaces",
					ei.Index())
			}
			ns := findPMemInScan(ssr, mount.DeviceList)
			if ns == nil {
				return nil, errors.Errorf("instance %d: no pmem namespace for mount %s",
					ei.Index(), mount.Path)
			}
			ns.Mount = mount
			nss[idx] = ns
		}

		c.log.Debugf("updated scm fs usage on device %s mounted at %s: %+v",
			nss[idx].BlockDevice, mount.Path, nss[idx].Mount)
	}

	return &storage.ScmScanResponse{Namespaces: nss}, nil
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

// ScmScan scans locally attached modules, namespaces and state of DCPM config.
func (c *StorageControlService) ScmScan(req storage.ScmScanRequest) (*storage.ScmScanResponse, error) {
	return c.storage.Scm.Scan(req)
}

// NvmePrepare preps locally attached SSDs and returns error.
func (c *StorageControlService) NvmePrepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	c.log.Debugf("calling bdev provider prepare: %+v", req)
	return c.storage.PrepareBdevs(req)
}

// mapCtrlrs maps each controller to it's PCI address.
func mapCtrlrs(ctrlrs storage.NvmeControllers) (map[string]*storage.NvmeController, error) {
	ctrlrMap := make(map[string]*storage.NvmeController)

	for _, ctrlr := range ctrlrs {
		if _, exists := ctrlrMap[ctrlr.PciAddr]; exists {
			return nil, errors.Errorf("duplicate entries for controller %s",
				ctrlr.PciAddr)
		}

		ctrlrMap[ctrlr.PciAddr] = ctrlr
	}

	return ctrlrMap, nil
}

// scanAssignedBdevs retrieves up-to-date NVMe controller info including
// health statistics and stored server meta-data. If I/O Engines are running
// then query is issued over dRPC as go-spdk bindings cannot be used to access
// controller claimed by another process. Only update info for controllers
// assigned to I/O Engines.
func (c *ControlService) scanAssignedBdevs(ctx context.Context, statsReq bool) (*storage.BdevScanResponse, error) {
	var ctrlrs storage.NvmeControllers
	instances := c.harness.Instances()

	for _, ei := range instances {
		if !ei.HasBlockDevices() {
			continue
		}

		tsrs, err := ei.ScanBdevTiers()
		if err != nil {
			return nil, err
		}

		// If the is not running or we aren't interested in temporal
		// statistics for the bdev devices then continue to next.
		if !ei.IsReady() || !statsReq {
			for _, tsr := range tsrs {
				ctrlrs = ctrlrs.Update(tsr.Result.Controllers...)
			}
			continue
		}

		// If engine is running and has claimed the assigned devices for
		// each tier, iterate over scan results for each tier and send query
		// over drpc to update controller details with current health stats
		// and smd info.
		for _, tsr := range tsrs {
			ctrlrMap, err := mapCtrlrs(tsr.Result.Controllers)
			if err != nil {
				return nil, errors.Wrap(err, "create controller map")
			}

			if err := ei.updateInUseBdevs(ctx, ctrlrMap); err != nil {
				return nil, errors.Wrap(err, "updating bdev health and smd info")
			}

			ctrlrs = ctrlrs.Update(tsr.Result.Controllers...)
		}
	}

	return &storage.BdevScanResponse{Controllers: ctrlrs}, nil
}

// NvmeScan scans locally attached SSDs.
func (c *StorageControlService) NvmeScan(req storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
	return c.storage.ScanBdevs(req)
}
