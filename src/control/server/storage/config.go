//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
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
)

type Class string

func (c *Class) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var tmp string
	if err := unmarshal(&tmp); err != nil {
		return err
	}

	class := Class(tmp)
	switch class {
	case ClassDCPM, ClassRAM,
		ClassNvme, ClassFile, ClassKdev, ClassMalloc:
		*c = class
	default:
		return errors.Errorf("unsupported storage class %q", tmp)
	}
	return nil
}

func (s Class) String() string {
	return string(s)
}

const (
	ClassNone   Class = ""
	ClassDCPM   Class = "dcpm"
	ClassRAM    Class = "ram"
	ClassNvme   Class = "nvme"
	ClassMalloc Class = "malloc"
	ClassKdev   Class = "kdev"
	ClassFile   Class = "file"
)

type Config struct {
	Tier  int        `yaml:"-"`
	Class Class      `yaml:"class"`
	Scm   ScmConfig  `yaml:",inline"`
	Bdev  BdevConfig `yaml:",inline"`
}

func NewConfig() *Config {
	return new(Config)
}

func (c *Config) IsSCM() bool {
	switch c.Class {
	case ClassDCPM, ClassRAM:
		return true
	default:
		return false
	}
}

func (c *Config) IsBdev() bool {
	switch c.Class {
	case ClassNvme, ClassFile, ClassKdev, ClassMalloc:
		return true
	default:
		return false
	}
}

func (c *Config) Validate() error {
	if c.IsSCM() {
		return c.Scm.Validate(c.Class)
	}
	if c.IsBdev() {
		return c.Bdev.Validate(c.Class)
	}

	return errors.New("no storage class set")
}

func (c *Config) WithTier(tier int) *Config {
	c.Tier = tier
	return c
}

// WithScmClass defines the type of SCM storage to be configured.
func (c *Config) WithScmClass(scmClass string) *Config {
	c.Class = Class(scmClass)
	return c
}

// WithScmMountPoint sets the path to the device used for SCM storage.
func (c *Config) WithScmMountPoint(scmPath string) *Config {
	c.Scm.MountPoint = scmPath
	return c
}

// WithScmRamdiskSize sets the size (in GB) of the ramdisk used
// to emulate SCM (no effect if ScmClass is not RAM).
func (c *Config) WithScmRamdiskSize(size uint) *Config {
	c.Scm.RamdiskSize = size
	return c
}

// WithScmDeviceList sets the list of devices to be used for SCM storage.
func (c *Config) WithScmDeviceList(devices ...string) *Config {
	c.Scm.DeviceList = devices
	return c
}

// WithBdevClass defines the type of block device storage to be used.
func (c *Config) WithBdevClass(bdevClass string) *Config {
	c.Class = Class(bdevClass)
	return c
}

// WithBdevDeviceList sets the list of block devices to be used.
func (c *Config) WithBdevDeviceList(devices ...string) *Config {
	c.Bdev.DeviceList = devices
	return c
}

// WithBdevDeviceCount sets the number of devices to be created when BdevClass is malloc.
func (c *Config) WithBdevDeviceCount(count int) *Config {
	c.Bdev.DeviceCount = count
	return c
}

// WithBdevFileSize sets the backing file size (used when BdevClass is malloc or file).
func (c *Config) WithBdevFileSize(size int) *Config {
	c.Bdev.FileSize = size
	return c
}

// WithBdevHostname sets the hostname to be used when generating NVMe configurations.
func (c *Config) WithBdevHostname(name string) *Config {
	c.Bdev.Hostname = name
	return c
}

type Configs []*Config

func (c Configs) Validate() error {
	for _, cfg := range c {
		if err := cfg.Validate(); err != nil {
			return errors.Wrapf(err, "tier %d failed validation", cfg.Tier)
		}
	}

	return nil
}

func (c Configs) ScmConfigs() (out []*Config) {
	for _, cfg := range c {
		if cfg.IsSCM() {
			out = append(out, cfg)
		}
	}
	return
}

func (c Configs) BdevConfigs() (out []*Config) {
	for _, cfg := range c {
		if cfg.IsBdev() {
			out = append(out, cfg)
		}
	}
	return
}

func (c *Configs) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var tmp []*Config
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

const (
	maxScmDeviceLen = 1
)

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
	case ClassDCPM:
		if sc.RamdiskSize > 0 {
			return errors.New("scm_size may not be set when scm_class is dcpm")
		}
		if len(sc.DeviceList) == 0 {
			return errors.New("scm_list must be set when scm_class is dcpm")
		}
	case ClassRAM:
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
	VmdDisabled bool     `yaml:"-"` // set during start-up
	DeviceCount int      `yaml:"bdev_number,omitempty"`
	FileSize    int      `yaml:"bdev_size,omitempty"`
	Hostname    string   `yaml:"-"` // used when generating templates
}

func (bc *BdevConfig) checkNonZeroFileSize(class Class) error {
	if bc.FileSize == 0 {
		return errors.Errorf("bdev_class %s requires non-zero bdev_size",
			class)
	}

	return nil
}

// Validate sanity checks engine bdev config parameters.
func (bc *BdevConfig) Validate(class Class) error {
	if common.StringSliceHasDuplicates(bc.DeviceList) {
		return errors.New("bdev_list contains duplicate pci addresses")
	}

	switch class {
	case ClassFile:
		if err := bc.checkNonZeroFileSize(class); err != nil {
			return err
		}
	case ClassMalloc:
		if err := bc.checkNonZeroFileSize(class); err != nil {
			return err
		}
		if bc.DeviceCount == 0 {
			return errors.Errorf("bdev_class %s requires non-zero bdev_number",
				class)
		}
	case ClassKdev:
		if len(bc.DeviceList) == 0 {
			return errors.Errorf("bdev_class %s requires non-empty bdev_list",
				class)
		}
	case ClassNvme:
		for _, pci := range bc.DeviceList {
			_, _, _, _, err := common.ParsePCIAddress(pci)
			if err != nil {
				return errors.Wrapf(err, "parse pci address %s", pci)
			}
		}
	}

	return nil
}

type StorageConfig struct {
	Tiers      Configs `yaml:"storage"`
	TiersNum   int     `yaml:"-" cmdLongFlag:"--storage_tiers" cmdShortFlag:"-T"`
	ConfigPath string  `yaml:"-" cmdLongFlag:"--nvme" cmdShortFlag:"-n"`
	MemSize    int     `yaml:"-" cmdLongFlag:"--mem_size,nonzero" cmdShortFlag:"-r,nonzero"`
	VosEnv     string  `yaml:"-" cmdEnv:"VOS_BDEV_CLASS"`
}
