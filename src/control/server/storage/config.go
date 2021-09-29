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

func (s Class) String() string {
	return string(s)
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

func (c *TierConfig) IsSCM() bool {
	switch c.Class {
	case ClassDcpm, ClassRam:
		return true
	default:
		return false
	}
}

func (c *TierConfig) IsBdev() bool {
	switch c.Class {
	case ClassNvme, ClassFile, ClassKdev:
		return true
	default:
		return false
	}
}

func (c *TierConfig) Validate() error {
	if c.IsSCM() {
		return c.Scm.Validate(c.Class)
	}
	if c.IsBdev() {
		return c.Bdev.Validate(c.Class)
	}

	return errors.New("no storage class set")
}

func (c *TierConfig) WithTier(tier int) *TierConfig {
	c.Tier = tier
	return c
}

// WithScmClass defines the type of SCM storage to be configured.
func (c *TierConfig) WithScmClass(scmClass string) *TierConfig {
	c.Class = Class(scmClass)
	return c
}

// WithScmMountPoint sets the path to the device used for SCM storage.
func (c *TierConfig) WithScmMountPoint(scmPath string) *TierConfig {
	c.Scm.MountPoint = scmPath
	return c
}

// WithScmRamdiskSize sets the size (in GB) of the ramdisk used
// to emulate SCM (no effect if ScmClass is not RAM).
func (c *TierConfig) WithScmRamdiskSize(size uint) *TierConfig {
	c.Scm.RamdiskSize = size
	return c
}

// WithScmDeviceList sets the list of devices to be used for SCM storage.
func (c *TierConfig) WithScmDeviceList(devices ...string) *TierConfig {
	c.Scm.DeviceList = devices
	return c
}

// WithBdevClass defines the type of block device storage to be used.
func (c *TierConfig) WithBdevClass(bdevClass string) *TierConfig {
	c.Class = Class(bdevClass)
	return c
}

// WithBdevDeviceList sets the list of block devices to be used.
func (c *TierConfig) WithBdevDeviceList(devices ...string) *TierConfig {
	c.Bdev.DeviceList = devices
	return c
}

// WithBdevDeviceCount sets the number of devices to be created when BdevClass is malloc.
func (c *TierConfig) WithBdevDeviceCount(count int) *TierConfig {
	c.Bdev.DeviceCount = count
	return c
}

// WithBdevFileSize sets the backing file size (used when BdevClass is malloc or file).
func (c *TierConfig) WithBdevFileSize(size int) *TierConfig {
	c.Bdev.FileSize = size
	return c
}

type TierConfigs []*TierConfig

func (sc *Config) Validate() error {
	if err := sc.Tiers.Validate(); err != nil {
		return errors.Wrap(err, "storage config validation failed")
	}

	var pruned TierConfigs
	for _, tier := range sc.Tiers {
		if tier.IsBdev() && len(tier.Bdev.DeviceList) == 0 {
			continue // prune empty bdev tier
		}
		pruned = append(pruned, tier)
	}
	sc.Tiers = pruned

	scmCfgs := sc.Tiers.ScmConfigs()
	bdevCfgs := sc.Tiers.BdevConfigs()

	if len(scmCfgs) == 0 {
		return errors.New("missing scm storage tier in config")
	}

	// set persistent location for engine bdev config file to be consumed by
	// provider backend, set to empty when no devices specified
	sc.ConfigOutputPath = ""
	if len(bdevCfgs) > 0 {
		sc.ConfigOutputPath = filepath.Join(scmCfgs[0].Scm.MountPoint, BdevOutConfName)

		switch bdevCfgs[0].Class {
		case ClassFile, ClassKdev:
			sc.VosEnv = "AIO"
		case ClassNvme:
			sc.VosEnv = "NVME"
		}
	}

	return nil
}

func (c TierConfigs) CfgHasBdevs() bool {
	for _, bc := range c.BdevConfigs() {
		if len(bc.Bdev.DeviceList) > 0 {
			return true
		}
	}
	return false
}

func (c TierConfigs) Validate() error {
	for _, cfg := range c {
		if err := cfg.Validate(); err != nil {
			return errors.Wrapf(err, "tier %d failed validation", cfg.Tier)
		}
	}

	return nil
}

func (c TierConfigs) ScmConfigs() (out []*TierConfig) {
	for _, cfg := range c {
		if cfg.IsSCM() {
			out = append(out, cfg)
		}
	}
	return
}

func (c TierConfigs) BdevConfigs() (out []*TierConfig) {
	for _, cfg := range c {
		if cfg.IsBdev() {
			out = append(out, cfg)
		}
	}
	return
}

func (c *TierConfigs) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var tmp []*TierConfig
	if err := unmarshal(&tmp); err != nil {
		return err
	}

	for i := range tmp {
		if tmp[i].Tier == 0 {
			tmp[i].Tier = i
		}
	}
	*c = tmp

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
		for _, pci := range bc.DeviceList {
			_, _, _, _, err := common.ParsePCIAddress(pci)
			if err != nil {
				return errors.Wrapf(err, "parse pci address %s", pci)
			}
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
