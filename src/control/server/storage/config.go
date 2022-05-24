//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"encoding/json"
	"fmt"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
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

// WithStorageClass defines the type of storage (scm or bdev) to be configured.
func (tc *TierConfig) WithStorageClass(cls string) *TierConfig {
	tc.Class = Class(cls)
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

// WithBdevDeviceList sets the list of block devices to be used.
func (tc *TierConfig) WithBdevDeviceList(devices ...string) *TierConfig {
	if set, err := NewBdevDeviceList(devices...); err == nil {
		tc.Bdev.DeviceList = set
	} else {
		tc.Bdev.DeviceList = &BdevDeviceList{stringBdevSet: common.StringSet{}}
		for _, d := range devices {
			tc.Bdev.DeviceList.stringBdevSet.Add(d)
		}
	}
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
	tc.Bdev.BusidRange = MustNewBdevBusRange(rangeStr)
	return tc
}

type TierConfigs []*TierConfig

func (tcs TierConfigs) CfgHasBdevs() bool {
	for _, bc := range tcs.BdevConfigs() {
		if bc.Bdev.DeviceList.Len() > 0 {
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

// BdevDeviceList represents a set of block device addresses.
type BdevDeviceList struct {
	// As this is the most common use case, we'll make the embedded type's methods
	// available directly on the type.
	hardware.PCIAddressSet

	// As a fallback for non-PCI bdevs, maintain a map of strings.
	stringBdevSet common.StringSet
}

// maybePCI does a quick check to see if a string could possibly be a PCI address.
func maybePCI(addr string) bool {
	comps := strings.Split(addr, ":")
	if len(comps) != 3 {
		return false
	}
	return (len(comps[0]) == 6 || len(comps[0]) == 4) && len(comps[1]) == 2 && len(comps[2]) >= 2
}

// fromStrings creates a BdevDeviceList from a list of strings.
func (bdl *BdevDeviceList) fromStrings(addrs []string) error {
	if bdl == nil {
		return errors.New("nil BdevDeviceList")
	}

	if bdl.stringBdevSet == nil {
		bdl.stringBdevSet = common.StringSet{}
	}

	for _, strAddr := range addrs {
		if !maybePCI(strAddr) {
			if err := bdl.stringBdevSet.AddUnique(strAddr); err != nil {
				return errors.Wrap(err, "bdev_list")
			}
			continue
		}

		addr, err := hardware.NewPCIAddress(strAddr)
		if err != nil {
			return errors.Wrap(err, "bdev_list")
		}

		if bdl.Contains(addr) {
			return errors.Errorf("bdev_list: duplicate PCI address %s", addr)
		}

		if err := bdl.Add(addr); err != nil {
			return errors.Wrap(err, "bdev_list")
		}
	}

	if len(bdl.stringBdevSet) > 0 && bdl.PCIAddressSet.Len() > 0 {
		return errors.New("bdev_list: cannot mix PCI and non-PCI block device addresses")
	}

	return nil
}

func (bdl *BdevDeviceList) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var tmp []string
	if err := unmarshal(&tmp); err != nil {
		return err
	}

	return bdl.fromStrings(tmp)
}

func (bdl *BdevDeviceList) MarshalYAML() (interface{}, error) {
	return bdl.Devices(), nil
}

func (bdl *BdevDeviceList) UnmarshalJSON(data []byte) error {
	var tmp []string
	if err := json.Unmarshal(data, &tmp); err != nil {
		return err
	}

	return bdl.fromStrings(tmp)
}

func (bdl *BdevDeviceList) MarshalJSON() ([]byte, error) {
	return json.Marshal(bdl.Devices())
}

// PCIAddressSetPtr returns a pointer to the underlying hardware.PCIAddressSet.
func (bdl *BdevDeviceList) PCIAddressSetPtr() *hardware.PCIAddressSet {
	if bdl == nil {
		return nil
	}

	return &bdl.PCIAddressSet
}

// Len returns the number of block devices in the list.
func (bdl *BdevDeviceList) Len() int {
	if bdl == nil {
		return 0
	}

	if bdl.PCIAddressSet.Len() > 0 {
		return bdl.PCIAddressSet.Len()
	}

	return len(bdl.stringBdevSet)
}

// Equals returns true if the two lists are equivalent.
func (bdl *BdevDeviceList) Equals(other *BdevDeviceList) bool {
	if bdl == nil || other == nil {
		return false
	}

	if bdl.Len() != other.Len() {
		return false
	}

	if bdl.PCIAddressSet.Len() > 0 {
		return bdl.PCIAddressSet.Equals(&other.PCIAddressSet)
	}

	for addr := range bdl.stringBdevSet {
		if _, ok := other.stringBdevSet[addr]; !ok {
			return false
		}
	}

	return true
}

// Devices returns a slice of strings representing the block device addresses.
func (bdl *BdevDeviceList) Devices() []string {
	if bdl == nil {
		return []string{}
	}

	if bdl.PCIAddressSet.Len() == 0 {
		return bdl.stringBdevSet.ToSlice()
	}

	var addresses []string
	for _, addr := range bdl.Addresses() {
		addresses = append(addresses, addr.String())
	}
	return addresses
}

func (bdl *BdevDeviceList) String() string {
	return strings.Join(bdl.Devices(), ",")
}

// NewBdevDeviceList creates a new BdevDeviceList from a list of strings.
func NewBdevDeviceList(devices ...string) (*BdevDeviceList, error) {
	bdl := &BdevDeviceList{stringBdevSet: common.StringSet{}}
	return bdl, bdl.fromStrings(devices)
}

// MustNewBdevDeviceList creates a new BdevDeviceList from a string representation of a set of block device addresses. Panics on error.
func MustNewBdevDeviceList(devices ...string) *BdevDeviceList {
	bdl, err := NewBdevDeviceList(devices...)
	if err != nil {
		panic(err)
	}
	return bdl
}

// BdevBusRange represents a bus-ID range to be used to filter hot plug events.
type BdevBusRange struct {
	hardware.PCIBus
}

func (br *BdevBusRange) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var tmp string
	if err := unmarshal(&tmp); err != nil {
		return err
	}

	lo, hi, err := parsePCIBusRange(tmp, hardware.PCIAddrBusBitSize)
	if err != nil {
		return errors.Wrapf(err, "failed to parse bus range %q", tmp)
	}

	br.LowAddress.Bus = lo
	br.HighAddress.Bus = hi

	return nil
}

func (br *BdevBusRange) MarshalYAML() (interface{}, error) {
	return br.String(), nil
}

func (br *BdevBusRange) String() string {
	if br == nil {
		return ""
	}
	return fmt.Sprintf("0x%02x-0x%02x", br.LowAddress.Bus, br.HighAddress.Bus)
}

// NewBdevBusRange creates a new BdevBusRange from a string.
func NewBdevBusRange(rangeStr string) (*BdevBusRange, error) {
	br := &BdevBusRange{}
	if err := br.UnmarshalYAML(func(v interface{}) error {
		return yaml.Unmarshal([]byte(rangeStr), v)
	}); err != nil {
		return nil, err
	}

	return br, nil
}

// MustNewBdevBusRange creates a new BdevBusRange from a string. Panics on error.
func MustNewBdevBusRange(rangeStr string) *BdevBusRange {
	br, err := NewBdevBusRange(rangeStr)
	if err != nil {
		panic(err)
	}
	return br
}

// BdevConfig represents a Block Device (NVMe, etc.) configuration entry.
type BdevConfig struct {
	DeviceList  *BdevDeviceList `yaml:"bdev_list,omitempty"`
	DeviceCount int             `yaml:"bdev_number,omitempty"`
	FileSize    int             `yaml:"bdev_size,omitempty"`
	BusidRange  *BdevBusRange   `yaml:"bdev_busid_range,omitempty"`
}

func (bc *BdevConfig) checkNonZeroDevFileSize(class Class) error {
	if bc.FileSize == 0 {
		return errors.Errorf("bdev_class %s requires non-zero bdev_size",
			class)
	}

	return nil
}

func (bc *BdevConfig) checkNonEmptyDevList(class Class) error {
	if bc.DeviceList == nil || bc.DeviceList.Len() == 0 {
		return errors.Errorf("bdev_class %s requires non-empty bdev_list",
			class)
	}

	return nil
}

// Validate sanity checks engine bdev config parameters and update VOS env.
func (bc *BdevConfig) Validate(class Class) error {
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
		// NB: We are specifically checking that the embedded PCIAddressSet is non-empty.
		if bc.DeviceList == nil || bc.DeviceList.PCIAddressSet.Len() == 0 {
			return errors.New("bdev_class nvme requires valid PCI addresses in bdev_list")
		}
	default:
		return errors.Errorf("bdev_class value %q not supported (valid: nvme/kdev/file)", class)
	}

	return nil
}

// parsePCIBusRange takes a string of format <Begin-End> and returns the begin and end values.
// Number base is detected from the string prefixes e.g. 0x for hexadecimal.
// bitSize parameter sets a cut-off for the return values e.g. 8 for uint8.
func parsePCIBusRange(numRange string, bitSize int) (uint8, uint8, error) {
	if numRange == "" {
		return 0, 0, nil
	}

	split := strings.Split(numRange, "-")
	if len(split) != 2 {
		return 0, 0, errors.Errorf("invalid busid range %q", numRange)
	}

	begin, err := strconv.ParseUint(split[0], 0, bitSize)
	if err != nil {
		return 0, 0, errors.Wrapf(err, "parse busid range %q", numRange)
	}

	end, err := strconv.ParseUint(split[1], 0, bitSize)
	if err != nil {
		return 0, 0, errors.Wrapf(err, "parse busid range %q", numRange)
	}

	if begin > end {
		return 0, 0, errors.Errorf("invalid busid range %q", numRange)
	}

	return uint8(begin), uint8(end), nil
}

type Config struct {
	Tiers            TierConfigs `yaml:"storage" cmdLongFlag:"--storage_tiers,nonzero,count" cmdShortFlag:"-T,nonzero,count"`
	ConfigOutputPath string      `yaml:"-" cmdLongFlag:"--nvme" cmdShortFlag:"-n"`
	VosEnv           string      `yaml:"-" cmdEnv:"VOS_BDEV_CLASS"`
	EnableHotplug    bool        `yaml:"-"`
	NumaNodeIndex    uint        `yaml:"-"`
}

func (c *Config) Validate() error {
	if err := c.Tiers.Validate(); err != nil {
		return errors.Wrap(err, "storage config validation failed")
	}

	var pruned TierConfigs
	for _, tier := range c.Tiers {
		if tier.IsBdev() && tier.Bdev.DeviceList.Len() == 0 {
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

	return nil
}
