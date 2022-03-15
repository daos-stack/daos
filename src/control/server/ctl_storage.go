//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
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
	getHugePageInfo common.GetHugePageInfoFn
}

// GetScmState performs required initialization and returns current state
// of SCM module preparation.
func (scs *StorageControlService) GetScmState() (storage.ScmState, error) {
	return scs.storage.Scm.GetPmemState()
}

// ScmPrepare preps locally attached modules and returns need to reboot message,
// list of pmem device files and error directly.
//
// Suitable for commands invoked directly on server, not over gRPC.
func (scs *StorageControlService) ScmPrepare(req storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error) {
	// transition to the next state in SCM preparation
	return scs.storage.Scm.Prepare(req)
}

// ScmScan scans locally attached modules, namespaces and state of DCPM config.
func (scs *StorageControlService) ScmScan(req storage.ScmScanRequest) (*storage.ScmScanResponse, error) {
	return scs.storage.Scm.Scan(req)
}

// NvmePrepare preps locally attached SSDs and returns error.
func (scs *StorageControlService) NvmePrepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	scs.log.Debugf("calling bdev provider prepare: %+v", req)
	return scs.storage.PrepareBdevs(req)
}

// NvmeScan scans locally attached SSDs.
func (scs *StorageControlService) NvmeScan(req storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
	scs.log.Debugf("calling bdev provider scan: %+v", req)
	return scs.storage.ScanBdevs(req)
}

// WithVMDEnabled enables VMD support in storage provider.
func (scs *StorageControlService) WithVMDEnabled() *StorageControlService {
	scs.storage.WithVMDEnabled()
	return scs
}

func newStorageControlService(l logging.Logger, ecs []*engine.Config, sp *storage.Provider, hpiFn common.GetHugePageInfoFn) *StorageControlService {
	instanceStorage := make(map[uint32]*storage.Config)
	for i, c := range ecs {
		instanceStorage[uint32(i)] = &c.Storage
	}

	return &StorageControlService{
		log:             l,
		storage:         sp,
		instanceStorage: instanceStorage,
		getHugePageInfo: hpiFn,
	}
}

// NewStorageControlService returns an initialized *StorageControlService
func NewStorageControlService(log logging.Logger, engineCfgs []*engine.Config) *StorageControlService {
	return newStorageControlService(log, engineCfgs,
		storage.DefaultProvider(log, 0, &storage.Config{
			Tiers: nil,
		}),
		common.GetHugePageInfo,
	)
}

// NewMockStorageControlService returns a StorageControlService with a mocked
// storage provider consisting of the given sys, scm and bdev providers.
func NewMockStorageControlService(log logging.Logger, engineCfgs []*engine.Config, sys storage.SystemProvider, scm storage.ScmProvider, bdev storage.BdevProvider) *StorageControlService {
	return newStorageControlService(log, engineCfgs,
		storage.MockProvider(log, 0, &storage.Config{
			Tiers: nil,
		}, sys, scm, bdev),
		func() (*common.HugePageInfo, error) {
			return nil, nil
		},
	)
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
func (cs *ControlService) getScmUsage(ssr *storage.ScmScanResponse) (*storage.ScmScanResponse, error) {
	if ssr == nil {
		return nil, errors.New("input scm scan response is nil")
	}

	instances := cs.harness.Instances()

	nss := make(storage.ScmNamespaces, len(instances))
	for idx, ei := range instances {
		if !ei.IsReady() {
			continue // skip if not running
		}

		cfg, err := ei.GetStorage().GetScmConfig()
		if err != nil {
			return nil, err
		}

		mount, err := ei.GetStorage().GetScmUsage()
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

		cs.log.Debugf("updated scm fs usage on device %s mounted at %s: %+v",
			nss[idx].BlockDevice, mount.Path, nss[idx].Mount)
	}

	return &storage.ScmScanResponse{Namespaces: nss}, nil
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
func (cs *ControlService) scanAssignedBdevs(ctx context.Context, statsReq bool) (*storage.BdevScanResponse, error) {
	instances := cs.harness.Instances()
	ctrlrs := storage.NvmeControllers{}

	for _, ei := range instances {
		if !ei.GetStorage().HasBlockDevices() {
			continue
		}

		tsrs, err := ei.ScanBdevTiers()
		if err != nil {
			return nil, err
		}

		// If the engine is not running or we aren't interested in temporal
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
				return nil, err
			}

			ctrlrs = ctrlrs.Update(tsr.Result.Controllers...)
		}
	}

	return &storage.BdevScanResponse{Controllers: ctrlrs}, nil
}
