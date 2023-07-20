//
// (C) Copyright 2019-2023 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// StorageControlService encapsulates the storage part of the control service
type StorageControlService struct {
	log             logging.Logger
	storage         *storage.Provider
	instanceStorage map[uint32]*storage.Config
	getMemInfo      common.GetMemInfoFn
}

// ScmPrepare preps locally attached modules.
func (scs *StorageControlService) ScmPrepare(req storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error) {
	return scs.storage.PrepareScm(req)
}

// ScmScan scans locally attached modules and namespaces.
func (scs *StorageControlService) ScmScan(req storage.ScmScanRequest) (*storage.ScmScanResponse, error) {
	return scs.storage.ScanScm(req)
}

// NvmePrepare preps locally attached SSDs.
func (scs *StorageControlService) NvmePrepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	return scs.storage.PrepareBdevs(req)
}

// NvmeScan scans locally attached SSDs.
func (scs *StorageControlService) NvmeScan(req storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
	return scs.storage.ScanBdevs(req)
}

// WithVMDEnabled enables VMD support in storage provider.
func (scs *StorageControlService) WithVMDEnabled() *StorageControlService {
	scs.storage.WithVMDEnabled()
	return scs
}

// NewStorageControlService returns an initialized *StorageControlService
func NewStorageControlService(log logging.Logger, ecs []*engine.Config) *StorageControlService {
	topCfg := &storage.Config{
		Tiers: nil,
	}
	if len(ecs) > 0 {
		topCfg.ControlMetadata = ecs[0].Storage.ControlMetadata
	}
	instanceStorage := make(map[uint32]*storage.Config)
	for i, c := range ecs {
		instanceStorage[uint32(i)] = &c.Storage
	}

	return &StorageControlService{
		log:             log,
		instanceStorage: instanceStorage,
		storage:         storage.DefaultProvider(log, 0, topCfg),
		getMemInfo:      common.GetMemInfo,
	}
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

		if nss[idx].Mount != nil {
			rank, err := ei.GetRank()
			if err != nil {
				return nil, errors.Wrapf(err, "instance %d: no rank associated for mount %s",
					ei.Index(), mount.Path)
			}
			nss[idx].Mount.Rank = rank
		}

		cs.log.Debugf("updated scm fs usage on device %s mounted at %s: %+v",
			nss[idx].BlockDevice, mount.Path, nss[idx].Mount)
	}

	return &storage.ScmScanResponse{Namespaces: nss}, nil
}

// scanAssignedBdevs retrieves up-to-date NVMe controller info including
// health statistics and stored server meta-data. If I/O Engines are running
// then query is issued over dRPC as go-spdk bindings cannot be used to access
// controller claimed by another process. Only update info for controllers
// assigned to I/O Engines.
func (cs *ControlService) scanAssignedBdevs(ctx context.Context, nsps []*ctl.ScmNamespace, statsReq bool) (*storage.BdevScanResponse, error) {
	instances := cs.harness.Instances()
	ctrlrs := new(storage.NvmeControllers)

	for _, ei := range instances {
		if !ei.GetStorage().HasBlockDevices() {
			continue
		}

		tsrs, err := ei.ScanBdevTiers()
		if err != nil {
			return nil, err
		}

		// Build slice of controllers in all tiers.
		tierCtrlrs := make([]storage.NvmeController, 0)
		msg := fmt.Sprintf("NVMe tiers for engine-%d:", ei.Index())
		for _, tsr := range tsrs {
			msg += fmt.Sprintf("\n\tTier-%d: %s", tsr.Tier, tsr.Result.Controllers)
			for _, c := range tsr.Result.Controllers {
				tierCtrlrs = append(tierCtrlrs, *c)
			}
		}
		cs.log.Info(msg)

		// If the engine is not running or we aren't interested in temporal
		// statistics for the bdev devices then continue to next engine.
		if !ei.IsReady() || !statsReq {
			ctrlrs.Update(tierCtrlrs...)
			continue
		}

		cs.log.Debugf("updating stats for %d bdev(s) on instance %d", len(tierCtrlrs),
			ei.Index())

		// DAOS-12750 Compute the maximal size of the metadata to allow the engine to fill
		// the WallMeta field response.  The maximal metadata (i.e. VOS index file) size
		// should be equal to the SCM available size divided by the number of targets of the
		// engine.
		var md_size uint64
		var rdb_size uint64
		for _, nsp := range nsps {
			mp := nsp.GetMount()
			if mp == nil {
				continue
			}
			if r, err := ei.GetRank(); err != nil || uint32(r) != mp.GetRank() {
				continue
			}

			md_size = mp.GetUsableBytes() / uint64(ei.GetTargetCount())

			engineCfg, err := cs.getEngineCfgFromScmNsp(nsp)
			if err != nil {
				return nil, errors.Wrap(err, "Engine with invalid configuration")
			}
			rdb_size, err = cs.getRdbSize(engineCfg)
			if err != nil {
				return nil, err
			}
			break
		}

		if md_size == 0 {
			cs.log.Noticef("instance %d: no SCM space available for metadata", ei.Index)
		}

		// If engine is running and has claimed the assigned devices for
		// each tier, iterate over scan results for each tier and send query
		// over drpc to update controller details with current health stats
		// and smd info.
		updatedCtrlrs, err := ei.updateInUseBdevs(ctx, tierCtrlrs, md_size, rdb_size)
		if err != nil {
			return nil, errors.Wrapf(err, "instance %d: update online bdevs", ei.Index())
		}

		ctrlrs.Update(updatedCtrlrs...)
	}

	return &storage.BdevScanResponse{Controllers: *ctrlrs}, nil
}
