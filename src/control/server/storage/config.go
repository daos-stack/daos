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
	// MinNVMeStorage defines the minimum per-target allocation that may be
	// requested. Requests with smaller amounts will be rounded up.
	MinNVMeStorage = 1 << 30 // 1GiB, from bio_xtream.c

	// MinScmToNVMeRatio defines the minimum-allowable ratio of SCM to NVMe.
	MinScmToNVMeRatio = 0.01 // 1%

	// DefaultScmToNVMeRatio defines the default ratio of SCM to NVMe.
	DefaultScmToNVMeRatio = 0.06

	// BdevOutConfName defines the name of the output file to contain details
	// of bdevs to be used by a DAOS engine.
	BdevOutConfName = "daos_nvme.conf"

	maxScmDeviceLen = 1
)

// ScmClass definitions.
const (
	ScmClassNone ScmClass = ""
	ScmClassDCPM ScmClass = "dcpm"
	ScmClassRAM  ScmClass = "ram"
)

// ScmClass specifies device type for Storage Class Memory
type ScmClass string

// UnmarshalYAML implements yaml.Unmarshaler on ScmClass type
func (s *ScmClass) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var class string
	if err := unmarshal(&class); err != nil {
		return err
	}

	scmClass := ScmClass(class)
	switch scmClass {
	case ScmClassDCPM, ScmClassRAM:
		*s = scmClass
	default:
		return errors.Errorf("scm_class value %q not supported in config (dcpm/ram)", scmClass)
	}
	return nil
}

func (s ScmClass) String() string {
	return string(s)
}

// ScmConfig represents a SCM (Storage Class Memory) configuration entry.
type ScmConfig struct {
	MountPoint  string   `yaml:"scm_mount,omitempty" cmdLongFlag:"--storage" cmdShortFlag:"-s"`
	Class       ScmClass `yaml:"scm_class,omitempty"`
	RamdiskSize int      `yaml:"scm_size,omitempty"`
	DeviceList  []string `yaml:"scm_list,omitempty"`
}

// Validate sanity checks engine scm config parameters.
func (sc *ScmConfig) Validate() error {
	if sc.MountPoint == "" {
		return errors.New("no scm_mount set")
	}
	if sc.RamdiskSize < 0 {
		return errors.New("negative scm_size")
	}

	switch sc.Class {
	case ScmClassDCPM:
		if sc.RamdiskSize > 0 {
			return errors.New("scm_size may not be set when scm_class is dcpm")
		}
		if len(sc.DeviceList) == 0 {
			return errors.New("scm_list must be set when scm_class is dcpm")
		}
	case ScmClassRAM:
		if sc.RamdiskSize == 0 {
			return errors.New("scm_size may not be unset or 0 when scm_class is ram")
		}
		if len(sc.DeviceList) > 0 {
			return errors.New("scm_list may not be set when scm_class is ram")
		}
	case ScmClassNone:
		return errors.New("scm_class not set")
	}

	if len(sc.DeviceList) > maxScmDeviceLen {
		return errors.Errorf("scm_list may have at most %d devices", maxScmDeviceLen)
	}
	return nil
}

// BdevClass definitions.
const (
	BdevClassNvme BdevClass = "nvme"
	BdevClassKdev BdevClass = "kdev"
	BdevClassFile BdevClass = "file"
)

// BdevClass specifies block device type for block device storage
type BdevClass string

// UnmarshalYAML implements yaml.Unmarshaler on BdevClass type
func (b *BdevClass) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var class string
	if err := unmarshal(&class); err != nil {
		return err
	}
	*b = BdevClass(class)

	return nil
}

func (b BdevClass) String() string {
	return string(b)
}

// BdevConfig represents a Block Device (NVMe, etc.) configuration entry.
type BdevConfig struct {
	OutputPath  string    `yaml:"-" cmdLongFlag:"--nvme" cmdShortFlag:"-n"`
	Class       BdevClass `yaml:"bdev_class,omitempty"`
	DeviceList  []string  `yaml:"bdev_list,omitempty"`
	VmdDisabled bool      `yaml:"-"` // set during start-up
	FileSize    int       `yaml:"bdev_size,omitempty"`
	VosEnv      string    `yaml:"-" cmdEnv:"VOS_BDEV_CLASS"`
}

func (bc *BdevConfig) checkNonZeroDevFileSize() error {
	if bc.FileSize == 0 {
		return errors.Errorf("bdev_class %s requires non-zero bdev_size",
			bc.Class)
	}

	return nil
}

func (bc *BdevConfig) checkNonEmptyDevList() error {
	if len(bc.DeviceList) == 0 {
		return errors.Errorf("bdev_class %s requires non-empty bdev_list",
			bc.Class)
	}

	return nil
}

// Validate sanity checks engine bdev config parameters and update VOS env.
func (bc *BdevConfig) Validate() error {
	if common.StringSliceHasDuplicates(bc.DeviceList) {
		return errors.New("bdev_list contains duplicate pci addresses")
	}
	if bc.FileSize < 0 {
		return errors.New("negative bdev_size")
	}
	if string(bc.Class) == "" {
		bc.Class = BdevClassNvme // apply default if unset
	}

	switch bc.Class {
	case BdevClassFile:
		if err := bc.checkNonEmptyDevList(); err != nil {
			return err
		}
		if err := bc.checkNonZeroDevFileSize(); err != nil {
			return err
		}
		bc.VosEnv = "AIO"
	case BdevClassKdev:
		if err := bc.checkNonEmptyDevList(); err != nil {
			return err
		}
		bc.VosEnv = "AIO"
	case BdevClassNvme:
		for _, pci := range bc.DeviceList {
			_, _, _, _, err := common.ParsePCIAddress(pci)
			if err != nil {
				return errors.Wrapf(err, "parse pci address %s", pci)
			}
		}
		bc.VosEnv = "NVME"
	default:
		return errors.Errorf("bdev_class value %q not supported (valid: nvme/kdev/file)", bc.Class)
	}

	return nil
}

// GetNvmeDevs retrieves device list only if class is nvme.
func (bc *BdevConfig) GetNvmeDevs() []string {
	if bc.Class == BdevClassNvme {
		return bc.DeviceList
	}

	return []string{}
}
