//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"path/filepath"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

const (
	// MinNVMeStorage defines the minimum per-target allocation
	// that may be requested. Requests with smaller amounts will
	// be rounded up.
	MinNVMeStorage = 1 << 30 // 1GiB, from bio_xtream.c

	// MinScmToNVMeRatio defines the minimum-allowable ratio
	// of SCM to NVMe.
	MinScmToNVMeRatio = 0.01 // 1%
	// DefaultScmToNVMeRatio defines the default ratio of
	// SCM to NVMe.
	DefaultScmToNVMeRatio = 0.06

	// BdevOutConfName defines the name of the output file to contain details
	// of bdevs to be used by a DAOS engine.
	BdevOutConfName = "daos_nvme.conf"

	maxScmDeviceLen = 1
)

// Class indicates a specific type of storage.
type Class string

func (c *Class) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var tmp string
	if err := unmarshal(&tmp); err != nil {
		return err
	}

	class := Class(tmp)
	switch class {
	case ClassDcpm, ClassRam, ClassNvme, ClassFile, ClassKdev:
		*c = class
	default:
		return errors.Errorf("unsupported storage class %q", tmp)
	}
	return nil
}

func (c Class) String() string {
	return string(c)
}

// Class type definitions.
const (
	ClassNone Class = ""
	ClassDcpm Class = "dcpm"
	ClassRam  Class = "ram"
	ClassNvme Class = "nvme"
	ClassKdev Class = "kdev"
	ClassFile Class = "file"
)

type TierConfig struct {
	Tier  int        `yaml:"-"`
	Class Class      `yaml:"class"`
	Scm   ScmConfig  `yaml:",inline"`
	Bdev  BdevConfig `yaml:",inline"`
}

func NewTierConfig() *TierConfig {
	return new(TierConfig)
}

func (tc *TierConfig) IsSCM() bool {
	switch tc.Class {
	case ClassDcpm, ClassRam:
		return true
	default:
		return false
	}
}

func (tc *TierConfig) IsBdev() bool {
	switch tc.Class {
	case ClassNvme, ClassFile, ClassKdev:
		return true
	default:
		return false
	}
}

func (tc *TierConfig) Validate() error {
	if tc.IsSCM() {
		return tc.Scm.Validate(tc.Class)
	}
	if tc.IsBdev() {
		return tc.Bdev.Validate(tc.Class)
	}

	return errors.New("no storage class set")
}

func (tc *TierConfig) WithTier(tier int) *TierConfig {
	tc.Tier = tier
	return tc
}

// WithScmClass defines the type of SCM storage to be configured.
func (tc *TierConfig) WithScmClass(scmClass string) *TierConfig {
	tc.Class = Class(scmClass)
	return tc
}

// WithScmMountPoint sets the path to the device used for SCM storage.
func (tc *TierConfig) WithScmMountPoint(scmPath string) *TierConfig {
	tc.Scm.MountPoint = scmPath
	return tc
}

// WithScmRamdiskSize sets the size (in GB) of the ramdisk used
// to emulate SCM (no effect if ScmClass is not RAM).
func (tc *TierConfig) WithScmRamdiskSize(size uint) *TierConfig {
	tc.Scm.RamdiskSize = size
	return tc
}

// WithScmDeviceList sets the list of devices to be used for SCM storage.
func (tc *TierConfig) WithScmDeviceList(devices ...string) *TierConfig {
	tc.Scm.DeviceList = devices
	return tc
}

// WithBdevClass defines the type of block device storage to be used.
func (tc *TierConfig) WithBdevClass(bdevClass string) *TierConfig {
	tc.Class = Class(bdevClass)
	return tc
}

// WithBdevDeviceList sets the list of block devices to be used.
func (tc *TierConfig) WithBdevDeviceList(devices ...string) *TierConfig {
	tc.Bdev.DeviceList = devices
	return tc
}

// WithBdevDeviceCount sets the number of devices to be created when BdevClass is malloc.
func (tc *TierConfig) WithBdevDeviceCount(count int) *TierConfig {
	tc.Bdev.DeviceCount = count
	return tc
}

// WithBdevFileSize sets the backing file size (used when BdevClass is malloc or file).
func (tc *TierConfig) WithBdevFileSize(size int) *TierConfig {
	tc.Bdev.FileSize = size
	return tc
}

// WithBdevBusidRange sets the bus-ID range to be used to filter hot plug events.
func (tc *TierConfig) WithBdevBusidRange(rangeStr string) *TierConfig {
	tc.Bdev.BusidRange = rangeStr
	return tc
}

type TierConfigs []*TierConfig

func (tcs TierConfigs) CfgHasBdevs() bool {
	for _, bc := range tcs.BdevConfigs() {
		if len(bc.Bdev.DeviceList) > 0 {
			return true
		}
	}

	return false
}

func (tcs TierConfigs) Validate() error {
	for _, cfg := range tcs {
		if err := cfg.Validate(); err != nil {
			return errors.Wrapf(err, "tier %d failed validation", cfg.Tier)
		}
	}
	return nil
}

func (tcs TierConfigs) ScmConfigs() (out []*TierConfig) {
	for _, cfg := range tcs {
		if cfg.IsSCM() {
			out = append(out, cfg)
		}
	}

	return
}

func (tcs TierConfigs) BdevConfigs() (out []*TierConfig) {
	for _, cfg := range tcs {
		if cfg.IsBdev() {
			out = append(out, cfg)
		}
	}

	return
}

func (tcs *TierConfigs) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var tmp []*TierConfig
	if err := unmarshal(&tmp); err != nil {
		return err
	}

	for i := range tmp {
		if tmp[i].Tier == 0 {
			tmp[i].Tier = i
		}
	}
	*tcs = tmp

	return nil
}

// ScmConfig represents a SCM (Storage Class Memory) configuration entry.
type ScmConfig struct {
	MountPoint  string   `yaml:"scm_mount,omitempty" cmdLongFlag:"--storage" cmdShortFlag:"-s"`
	RamdiskSize uint     `yaml:"scm_size,omitempty"`
	DeviceList  []string `yaml:"scm_list,omitempty"`
}

// Validate sanity checks engine scm config parameters.
func (sc *ScmConfig) Validate(class Class) error {
	if sc.MountPoint == "" {
		return errors.New("no scm_mount set")
	}

	switch class {
	case ClassDcpm:
		if sc.RamdiskSize > 0 {
			return errors.New("scm_size may not be set when scm_class is dcpm")
		}
		if len(sc.DeviceList) == 0 {
			return errors.New("scm_list must be set when scm_class is dcpm")
		}
	case ClassRam:
		if sc.RamdiskSize == 0 {
			return errors.New("scm_size may not be unset or 0 when scm_class is ram")
		}
		if len(sc.DeviceList) > 0 {
			return errors.New("scm_list may not be set when scm_class is ram")
		}
	}

	if len(sc.DeviceList) > maxScmDeviceLen {
		return errors.Errorf("scm_list may have at most %d devices", maxScmDeviceLen)
	}
	return nil
}

// BdevConfig represents a Block Device (NVMe, etc.) configuration entry.
type BdevConfig struct {
	DeviceList  []string `yaml:"bdev_list,omitempty"`
	DeviceCount int      `yaml:"bdev_number,omitempty"`
	FileSize    int      `yaml:"bdev_size,omitempty"`
	BusidRange  string   `yaml:"bdev_busid_range,omitempty"`
}

func (bc *BdevConfig) checkNonZeroDevFileSize(class Class) error {
	if bc.FileSize == 0 {
		return errors.Errorf("bdev_class %s requires non-zero bdev_size",
			class)
	}

	return nil
}

func (bc *BdevConfig) checkNonEmptyDevList(class Class) error {
	if len(bc.DeviceList) == 0 {
		return errors.Errorf("bdev_class %s requires non-empty bdev_list",
			class)
	}

	return nil
}

// Validate sanity checks engine bdev config parameters and update VOS env.
func (bc *BdevConfig) Validate(class Class) error {
	if common.StringSliceHasDuplicates(bc.DeviceList) {
		return errors.New("bdev_list contains duplicate pci addresses")
	}
	if bc.FileSize < 0 {
		return errors.New("negative bdev_size")
	}

	switch class {
	case ClassFile:
		if err := bc.checkNonEmptyDevList(class); err != nil {
			return err
		}
		if err := bc.checkNonZeroDevFileSize(class); err != nil {
			return err
		}
	case ClassKdev:
		if err := bc.checkNonEmptyDevList(class); err != nil {
			return err
		}
	case ClassNvme:
		if _, err := common.NewPCIAddressSet(bc.DeviceList...); err != nil {
			return errors.Wrapf(err, "parse pci addresses %v", bc.DeviceList)
		}
	default:
		return errors.Errorf("bdev_class value %q not supported (valid: nvme/kdev/file)", class)
	}

	return nil
}

type Config struct {
	Tiers            TierConfigs `yaml:"storage" cmdLongFlag:"--storage_tiers,nonzero" cmdShortFlag:"-T,nonzero"`
	ConfigOutputPath string      `yaml:"-" cmdLongFlag:"--nvme" cmdShortFlag:"-n"`
	VosEnv           string      `yaml:"-" cmdEnv:"VOS_BDEV_CLASS"`
	EnableHotplug    bool        `yaml:"-"`
}

func (c *Config) Validate() error {
	if err := c.Tiers.Validate(); err != nil {
		return errors.Wrap(err, "storage config validation failed")
	}

	var pruned TierConfigs
	for _, tier := range c.Tiers {
		if tier.IsBdev() && len(tier.Bdev.DeviceList) == 0 {
			continue // prune empty bdev tier
		}
		pruned = append(pruned, tier)
	}
	c.Tiers = pruned

	scmCfgs := c.Tiers.ScmConfigs()
	bdevCfgs := c.Tiers.BdevConfigs()

	if len(scmCfgs) == 0 {
		return errors.New("missing scm storage tier in config")
	}

	// set persistent location for engine bdev config file to be consumed by provider
	// backend, set to empty when no devices specified
	if len(bdevCfgs) == 0 {
		c.ConfigOutputPath = ""
		return nil
	}
	c.ConfigOutputPath = filepath.Join(scmCfgs[0].Scm.MountPoint, BdevOutConfName)

	fbc := bdevCfgs[0]

	// set vos environment variable based on class of first bdev config
	if fbc.Class == ClassFile || fbc.Class == ClassKdev {
		c.VosEnv = "AIO"
		return nil
	}

	if fbc.Class != ClassNvme {
		return nil
	}

	c.VosEnv = "NVME"

	// if the first bdev config is of class nvme, validate bus-id range params
	if _, _, err := common.GetRangeLimits(fbc.Bdev.BusidRange); err != nil {
		return errors.Wrap(err, "parse busid range limits")
	}

	return nil
}
