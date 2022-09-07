//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// LegacyStorage struct contains the old format of specifying SCM and Bdev storage.
type LegacyStorage struct {
	storage.ScmConfig  `yaml:",inline,omitempty"`
	ScmClass           storage.Class `yaml:"scm_class,omitempty"`
	storage.BdevConfig `yaml:",inline,omitempty"`
	BdevClass          storage.Class `yaml:"bdev_class,omitempty"`
}

// WasDefined returns true if the LegacyStorage reference refers to a populated struct.
func (ls *LegacyStorage) WasDefined() bool {
	return ls.ScmClass != storage.ClassNone || ls.BdevClass != storage.ClassNone
}

// WithLegacyStorage populates the engine config field appropriately.
func (ec *Config) WithLegacyStorage(lc LegacyStorage) *Config {
	ec.LegacyStorage = lc
	return ec
}

// ConvertLegacyStorage takes engine config legacy storage field and populates relevant config
// storage tiers.
func (ec *Config) ConvertLegacyStorage(log logging.Logger, idx int) {
	ls := ec.LegacyStorage
	if ls.WasDefined() {
		log.Noticef("engine %d: Legacy storage configuration detected. Please "+
			"migrate to new-style storage configuration.", idx)
		var tierCfgs storage.TierConfigs
		if ls.ScmClass != storage.ClassNone {
			tierCfgs = append(tierCfgs,
				storage.NewTierConfig().
					WithStorageClass(ls.ScmClass.String()).
					WithScmDeviceList(ls.ScmConfig.DeviceList...).
					WithScmMountPoint(ls.MountPoint).
					WithScmRamdiskSize(ls.RamdiskSize),
			)
		}

		// Do not add bdev tier if BdevClass is none or nvme has no devices.
		bc := ls.BdevClass
		switch {
		case bc == storage.ClassNvme && ls.BdevConfig.DeviceList.Len() == 0:
			log.Debugf("legacy storage config conversion skipped for class "+
				"%s with empty bdev_list", storage.ClassNvme)
		case bc == storage.ClassNone:
			log.Debugf("legacy storage config bdev bonversion skipped for class %s",
				storage.ClassNone)
		default:
			tierCfgs = append(tierCfgs,
				storage.NewTierConfig().
					WithStorageClass(ls.BdevClass.String()).
					WithBdevDeviceCount(ls.DeviceCount).
					WithBdevDeviceList(
						ls.BdevConfig.DeviceList.Devices()...).
					WithBdevFileSize(ls.FileSize).
					WithBdevBusidRange(
						ls.BdevConfig.BusidRange.String()),
			)
		}
		ec.WithStorage(tierCfgs...)
		ec.LegacyStorage = LegacyStorage{}
	}
}
