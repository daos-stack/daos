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

	accelOptMoveName = "move"
	accelOptCRCName  = "crc"

	bdevRoleDataName = "data"
	bdevRoleMetaName = "meta"
	bdevRoleWALName  = "wal"
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

// SetNumaNodeIndex sets the NUMA node index for the tier.
func (tc *TierConfig) SetNumaNodeIndex(idx uint) {
	tc.Scm.NumaNodeIndex = idx
	tc.Bdev.NumaNodeIndex = idx
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

// WithBdevDeviceRoles sets the role assignments for the bdev tier.
func (tc *TierConfig) WithBdevDeviceRoles(bits int) *TierConfig {
	tc.Bdev.DeviceRoles = BdevDeviceRoles{OptionBits(bits)}
	return tc
}

// WithNumaNodeIndex sets the NUMA node index to be used for this tier.
func (tc *TierConfig) WithNumaNodeIndex(idx uint) *TierConfig {
	tc.SetNumaNodeIndex(idx)
	return tc
}

type TierConfigs []*TierConfig

func (tcs TierConfigs) getBdevs(nvmeOnly bool) *BdevDeviceList {
	bdevs := []string{}
	for _, bc := range tcs.BdevConfigs() {
		if nvmeOnly && bc.Class != ClassNvme {
			continue
		}
		bdevs = append(bdevs, bc.Bdev.DeviceList.Devices()...)
	}

	return MustNewBdevDeviceList(bdevs...)
}

func (tcs TierConfigs) Bdevs() *BdevDeviceList {
	return tcs.getBdevs(false)
}

func (tcs TierConfigs) NVMeBdevs() *BdevDeviceList {
	return tcs.getBdevs(true)
}

func (tcs TierConfigs) checkBdevs(nvmeOnly, emulOnly bool) bool {
	for _, bc := range tcs.BdevConfigs() {
		if bc.Bdev.DeviceList.Len() > 0 {
			switch {
			case nvmeOnly:
				if bc.Class == ClassNvme {
					return true
				}
			case emulOnly:
				if bc.Class != ClassNvme {
					return true
				}
			default:
				return true
			}
		}
	}

	return false
}

func (tcs TierConfigs) HaveBdevs() bool {
	return tcs.checkBdevs(false, false)
}

func (tcs TierConfigs) HaveRealNVMe() bool {
	return tcs.checkBdevs(true, false)
}

func (tcs TierConfigs) HaveEmulatedNVMe() bool {
	return tcs.checkBdevs(false, true)
}

func (tcs TierConfigs) Validate() error {
	for _, cfg := range tcs {
		if err := cfg.Validate(); err != nil {
			return errors.Wrapf(err, "tier %d failed validation", cfg.Tier)
		}
	}
	return nil
}

// Validation of bdev tier role assignments is based on the following assumptions and rules:
//
// - A role can only be assigned to one entire tier, i.e. tier 2 & 3 cannot both be assigned the
//   Meta role. This doesn’t apply to the Data role which can be applied to multiple tiers e.g.
//   in the case where > 3 bdev tiers exist.
// - All roles (WAL, Meta and Data) need to be assigned in bdev tiers if scm class is ram.
// - If the (first) scm tier is of class dcpm, then only one bdev tier with Data role is supported,
//   no third tier (for now).
func (tcs TierConfigs) validateBdevTierRoles() error {
	sc := tcs.ScmConfigs()[0]
	bcs := tcs.BdevConfigs()

	nrWALTiers := 1
	nrMetaTiers := 1
	if sc.Class == ClassDcpm {
		nrWALTiers = 0
		nrMetaTiers = 0
		if len(bcs) > 1 {
			return FaultBdevConfigMultiTiersWithDCPM
		}
	}

	var foundWALTiers, foundMetaTiers, foundDataTiers int
	for _, bc := range bcs {
		bits := bc.Bdev.DeviceRoles.OptionBits
		if (bits & BdevRoleWAL) != 0 {
			foundWALTiers++
		}
		if (bits & BdevRoleMeta) != 0 {
			foundMetaTiers++
		}
		if (bits & BdevRoleData) != 0 {
			foundDataTiers++
		}
	}

	if foundWALTiers != nrWALTiers {
		return FaultBdevConfigBadNrRoles("WAL", foundWALTiers, nrWALTiers)
	}
	if foundMetaTiers != nrMetaTiers {
		return FaultBdevConfigBadNrRoles("Meta", foundMetaTiers, nrMetaTiers)
	}
	// When bdev NVMe tiers exist, there should always be at least one Data tier.
	if foundDataTiers == 0 {
		return FaultBdevConfigBadNrRoles("Data", 0, 1)
	}

	return nil
}

// Set NVME class tier roles either based on explicit settings or heuristics.
//
// Role assignments will be decided based on the following rule set:
// - For 1 bdev tier, all roles will be assigned to that tier.
// - For 2 bdev tiers, WAL and Meta roles will be assigned to the first bdev tier and Data to
//   the second bdev tier.
// - For 3 or more bdev tiers, WAL role will be assigned to the first bdev tier, Meta to the
//   second bdev tier and Data to all remaining bdev tiers.
// - If the scm tier is of class dcpm, the first bdev tier should have the Data role only.
// - If emulated NVMe is present in bdev tiers, implicit role assignment is skipped.
func (tcs TierConfigs) assignBdevTierRoles() error {
	scs := tcs.ScmConfigs()
	bcs := tcs.BdevConfigs()

	// Require tier-0 to be a SCM tier.
	if len(scs) != 1 || scs[0].Tier != 0 {
		return errors.New("first storage tier is not scm")
	}

	// Skip role assignment and validation no NVMe tiers exist.
	if !tcs.HaveRealNVMe() {
		return nil
	}

	tiersWithoutRoles := make([]int, 0, len(bcs))
	for _, bc := range bcs {
		if bc.Bdev.DeviceRoles.IsEmpty() {
			tiersWithoutRoles = append(tiersWithoutRoles, bc.Tier)
		}
	}

	l := len(tiersWithoutRoles)
	switch {
	case l == 0:
		// All bdev tiers have assigned roles, skip implicit assignment.
		return tcs.validateBdevTierRoles()
	case l == len(bcs):
		// No assigned roles, fall-through to perform implicit assignment.
	default:
		return errors.Errorf("some bdev tiers are missing role assignments: %+v",
			tiersWithoutRoles)
	}

	// First bdev tier should be data if scm tier is DCPM.
	if scs[0].Class == ClassDcpm {
		tcs[1].WithBdevDeviceRoles(BdevRoleData)
		return tcs.validateBdevTierRoles()
	}

	// Apply role assignments.
	switch len(bcs) {
	case 1:
		tcs[1].WithBdevDeviceRoles(BdevRoleAll)
	case 2:
		tcs[1].WithBdevDeviceRoles(BdevRoleWAL | BdevRoleMeta)
		tcs[2].WithBdevDeviceRoles(BdevRoleData)
	default:
		tcs[1].WithBdevDeviceRoles(BdevRoleWAL)
		tcs[2].WithBdevDeviceRoles(BdevRoleMeta)
		for i := 3; i < len(tcs); i++ {
			tcs[i].WithBdevDeviceRoles(BdevRoleData)
		}
	}

	return tcs.validateBdevTierRoles()
}

func (tcs TierConfigs) ScmConfigs() (out TierConfigs) {
	for _, cfg := range tcs {
		if cfg.IsSCM() {
			out = append(out, cfg)
		}
	}

	return
}

func (tcs TierConfigs) BdevConfigs() (out TierConfigs) {
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
	MountPoint    string   `yaml:"scm_mount,omitempty" cmdLongFlag:"--storage" cmdShortFlag:"-s"`
	RamdiskSize   uint     `yaml:"scm_size,omitempty"`
	DeviceList    []string `yaml:"scm_list,omitempty"`
	NumaNodeIndex uint     `yaml:"-"`
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

// OptionBits is a type alias representing option flags as a bitset.
type OptionBits uint16

type optFlagMap map[string]OptionBits

func (ofm optFlagMap) keys() []string {
	keys := common.NewStringSet()
	for k := range ofm {
		keys.Add(k)
	}

	return keys.ToSlice()
}

// toStrings returns a slice of option names that have been set.
func (obs OptionBits) toStrings(optStr2Flag optFlagMap) []string {
	opts := common.NewStringSet()
	for str, flag := range optStr2Flag {
		if obs&flag == flag {
			opts.Add(str)
		}
	}

	return opts.ToSlice()
}

// fromStrings generates bitset referenced by the function receiver from the option names provided.
func (obs *OptionBits) fromStrings(optStr2Flag optFlagMap, opts ...string) error {
	if obs == nil {
		return errors.New("fromStrings() called on nil OptionBits")
	}

	for _, opt := range opts {
		flag, exists := optStr2Flag[opt]
		if !exists {
			return FaultBdevConfigOptFlagUnknown(opt, optStr2Flag.keys()...)
		}
		*obs |= flag
	}

	return nil
}

// IsEmpty returns true if no options have been set.
func (obs *OptionBits) IsEmpty() bool {
	return obs == nil || *obs == 0
}

var roleOptFlags = optFlagMap{
	bdevRoleDataName: BdevRoleData,
	bdevRoleMetaName: BdevRoleMeta,
	bdevRoleWALName:  BdevRoleWAL,
}

// BdevDeviceRoles is a bitset representing SSD role assignments (enabling Metadata-on-SSD).
type BdevDeviceRoles struct {
	OptionBits
}

func (bdr BdevDeviceRoles) MarshalYAML() (interface{}, error) {
	return bdr.toStrings(roleOptFlags), nil
}

func (bdr *BdevDeviceRoles) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var opts []string
	if err := unmarshal(&opts); err != nil {
		return err
	}

	return bdr.fromStrings(roleOptFlags, opts...)
}

func (bdr *BdevDeviceRoles) String() string {
	if bdr == nil {
		return "0"
	}
	return fmt.Sprintf("%d", bdr.OptionBits)
}

// BdevConfig represents a Block Device (NVMe, etc.) configuration entry.
type BdevConfig struct {
	DeviceList    *BdevDeviceList `yaml:"bdev_list,omitempty"`
	DeviceCount   int             `yaml:"bdev_number,omitempty"`
	FileSize      int             `yaml:"bdev_size,omitempty"`
	BusidRange    *BdevBusRange   `yaml:"bdev_busid_range,omitempty"`
	DeviceRoles   BdevDeviceRoles `yaml:"bdev_roles,omitempty"`
	NumaNodeIndex uint            `yaml:"-"`
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

var accelOptFlags = optFlagMap{
	accelOptCRCName:  AccelOptCRCFlag,
	accelOptMoveName: AccelOptMoveFlag,
}

// AccelOptionBits is a type alias representing acceleration capabilities as a bitset.
type AccelOptionBits = OptionBits

func (obs AccelOptionBits) MarshalYAML() (interface{}, error) {
	return obs.toStrings(accelOptFlags), nil
}

func (obs *AccelOptionBits) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var opts []string
	if err := unmarshal(&opts); err != nil {
		return err
	}

	return obs.fromStrings(accelOptFlags, opts...)
}

// AccelProps struct describes acceleration engine setting and optional capabilities expressed
// as a bitset. AccelProps is used both in YAML server config and JSON NVMe config files.
type AccelProps struct {
	Engine  string          `yaml:"engine,omitempty" json:"accel_engine"`
	Options AccelOptionBits `yaml:"options,omitempty" json:"accel_opts"`
}

func (ap *AccelProps) UnmarshalYAML(unmarshal func(interface{}) error) error {
	if ap == nil {
		return errors.New("attempt to unmarshal nil AccelProps")
	}

	type AccelPropsDefault AccelProps
	tmp := AccelPropsDefault{
		Engine: AccelEngineNone,
	}

	if err := unmarshal(&tmp); err != nil {
		return err
	}
	out := AccelProps(tmp)

	switch out.Engine {
	case AccelEngineNone:
		out.Options = 0
	case AccelEngineSPDK, AccelEngineDML:
		if out.Options == 0 {
			// If no options have been specified, all capabilities should be enabled.
			if err := out.Options.fromStrings(accelOptFlags, accelOptFlags.keys()...); err != nil {
				return err
			}
		}
	default:
		return FaultBdevAccelEngineUnknown(ap.Engine, AccelEngineSPDK, AccelEngineDML)
	}

	*ap = out

	return nil
}

type Config struct {
	Tiers            TierConfigs `yaml:"storage" cmdLongFlag:"--storage_tiers,nonzero" cmdShortFlag:"-T,nonzero"`
	ConfigOutputPath string      `yaml:"-" cmdLongFlag:"--nvme" cmdShortFlag:"-n"`
	VosEnv           string      `yaml:"-" cmdEnv:"VOS_BDEV_CLASS"`
	EnableHotplug    bool        `yaml:"-"`
	NumaNodeIndex    uint        `yaml:"-"`
	AccelProps       AccelProps  `yaml:"acceleration,omitempty"`
}

func (c *Config) SetNUMAAffinity(node uint) {
	c.NumaNodeIndex = node
	for _, tier := range c.Tiers {
		tier.SetNumaNodeIndex(node)
	}
}

func (c *Config) GetBdevs() *BdevDeviceList {
	return c.Tiers.Bdevs()
}

func (c *Config) GetNVMeBdevs() *BdevDeviceList {
	return c.Tiers.NVMeBdevs()
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

	if c.Tiers.HaveRealNVMe() && c.Tiers.HaveEmulatedNVMe() {
		return FaultBdevConfigTypeMismatch
	}

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

	return errors.Wrapf(c.Tiers.assignBdevTierRoles(), "invalid bdev tier roles requested")
}
