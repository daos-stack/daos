//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
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
	scs.storage.WithVMDEnabled(true)
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

	nss := make(storage.ScmNamespaces, 0, len(instances))
	for _, engine := range instances {
		if !engine.IsReady() {
			continue // skip if not running
		}

		cfg, err := engine.GetStorage().GetScmConfig()
		if err != nil {
			return nil, err
		}

		mount, err := engine.GetStorage().GetScmUsage()
		if err != nil {
			return nil, err
		}

		var ns *storage.ScmNamespace
		switch mount.Class {
		case storage.ClassRam: // generate fake namespace for emulated ramdisk mounts
			ns = &storage.ScmNamespace{
				Mount:       mount,
				BlockDevice: "ramdisk",
				Size:        uint64(humanize.GiByte * cfg.Scm.RamdiskSize),
			}
		case storage.ClassDcpm: // update namespace mount info for online storage
			if ssr.Namespaces == nil {
				return nil, errors.Errorf("instance %d: input scm scan response missing namespaces",
					engine.Index())
			}
			ns = findPMemInScan(ssr, mount.DeviceList)
			if ns == nil {
				return nil, errors.Errorf("instance %d: no pmem namespace for mount %s",
					engine.Index(), mount.Path)
			}
			ns.Mount = mount
		}

		if ns.Mount != nil {
			rank, err := engine.GetRank()
			if err != nil {
				return nil, errors.Wrapf(err, "instance %d: no rank associated for mount %s",
					engine.Index(), mount.Path)
			}
			ns.Mount.Rank = rank
		}

		cs.log.Debugf("updated scm fs usage on device %s mounted at %s: %+v", ns.BlockDevice,
			mount.Path, ns.Mount)
		nss = append(nss, ns)
	}

	if len(nss) == 0 {
		return nil, errors.New("no scm details found")
	}
	return &storage.ScmScanResponse{Namespaces: nss}, nil
}
