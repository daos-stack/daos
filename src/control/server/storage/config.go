//
// (C) Copyright 2019-2023 Intel Corporation.
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

	"github.com/dustin/go-humanize"
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

	maxNrBdevTiersWithoutRoles = 1
	maxNrBdevTiersWithRoles    = 3

	// ControlMetadataSubdir defines the name of the subdirectory to hold control metadata
	ControlMetadataSubdir = "daos_control"
)

// ControlMetadata describes configuration options for control plane metadata storage on the
// DAOS server.
type ControlMetadata struct {
	Path       string `yaml:"path,omitempty"`
	DevicePath string `yaml:"device,omitempty"`
}

// Directory returns the full path to the directory where the control plane metadata is saved.
func (cm ControlMetadata) Directory() string {
	if cm.Path == "" {
		return ""
	}
	return filepath.Join(cm.Path, ControlMetadataSubdir)
}

// EngineDirectory returns the full path to the directory where the per-engine metadata is saved.
func (cm ControlMetadata) EngineDirectory(idx uint) string {
	return ControlMetadataEngineDir(cm.Directory(), idx)
}

// HasPath returns true if the ControlMetadata path is set.
func (cm ControlMetadata) HasPath() bool {
	return cm.Path != ""
}

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

// WithScmDisableHugepages disables hugepages for tmpfs.
func (tc *TierConfig) WithScmDisableHugepages() *TierConfig {
	tc.Scm.DisableHugepages = true
	return tc
}

// WithScmMountPoint sets the path to the device used for SCM storage.
func (tc *TierConfig) WithScmMountPoint(scmPath string) *TierConfig {
	tc.Scm.MountPoint = scmPath
	return tc
}

// WithScmRamdiskSize sets the size (in GiB) of the ramdisk used
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
	tc.Bdev.DeviceRoles = BdevRoles{OptionBits(bits)}
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

func (tcs TierConfigs) HasBdevRoleMeta() bool {
	for _, bc := range tcs.BdevConfigs() {
		bits := bc.Bdev.DeviceRoles.OptionBits
		if (bits & BdevRoleMeta) != 0 {
			return true
		}
	}

	return false
}

func (tcs TierConfigs) Validate() error {
	if len(tcs) == 0 {
		return errors.New("no storage tiers configured")
	}

	scmCfgs := tcs.ScmConfigs()
	if len(scmCfgs) == 0 {
		return FaultScmConfigTierMissing
	}

	if tcs.HaveRealNVMe() && tcs.HaveEmulatedNVMe() {
		return FaultBdevConfigTierTypeMismatch
	}

	for _, cfg := range tcs {
		if err := cfg.Validate(); err != nil {
			return errors.Wrapf(err, "tier %d failed validation", cfg.Tier)
		}
	}

	return tcs.validateBdevRoles()
}

// Validation of configuration options and intended behavior to use or not use
// the MD-on-SSD code path are as follows:
//
//   - Exactly one storage tier with class: dcpm exists, no storage tier with class:
//     ram exists, and zero or one tier(s) with class: nvme exists. If an NVMe tier is
//     present, no bdev_roles: attributes are allowed on it.  This is the traditional
//     PMem-based configuration, obviously not using MD-on-SSD.
//
//   - No storage tier with class: dcpm exists, exactly one storage tier with class:
//     ram exists, and zero or one tier(s) with class: nvme exists. If an NVMe
//     tier is present, no bdev_roles: attributes are specified for the NVMe tier.
//     This is the traditional DRAM-based (ephemeral) configuration, and it shall not
//     use MD-on-SSD (to be compatible/consistent with earlier levels of DAOS software).
//
//   - No storage tier with class: dcpm exists, exactly one storage tier with class:
//     ram exists, and one, two or three tiers with class: nvme exist, with mandatory
//     bdev_roles: attributes on each of the NVMe tiers. Each of the three roles
//     (wal,meta,data) must be assigned to exactly one NVMe tier (no default
//     assignments of roles to tiers by the control plane software; all roles shall be
//     explicitly specified). This setup shall use the MD-on-SSD code path.  In this
//     scenario allow the use of a single NVMe tier co-locating all three roles, three
//     separate NVMe tiers with each tier dedicated to exactly one role, or two
//     separate NVMe tiers where two of the three roles are co-located on one of the
//     two NVMe tiers. In the latter case, all combinations to co-locate two of the
//     roles shall be allowed, although not all those combinations may be technically
//     desirable in production environments.
func (tcs TierConfigs) validateBdevRoles() error {
	scmConfs := tcs.ScmConfigs()
	if len(scmConfs) != 1 || scmConfs[0].Tier != 0 {
		return errors.New("first storage tier is not scm")
	}

	sc := scmConfs[0]
	bcs := tcs.BdevConfigs()

	var wal, meta, data int
	hasRoles := func() bool {
		return wal > 0 || meta > 0 || data > 0
	}

	for i, bc := range bcs {
		roles := bc.Bdev.DeviceRoles
		if roles.IsEmpty() {
			if hasRoles() {
				return FaultBdevConfigRolesMissing
			}
			continue
		}
		if i != 0 && !hasRoles() {
			return FaultBdevConfigRolesMissing
		}

		bits := roles.OptionBits
		hasWAL := (bits & BdevRoleWAL) != 0
		hasMeta := (bits & BdevRoleMeta) != 0
		hasData := (bits & BdevRoleData) != 0

		// Disallow having both wal and data only on a tier.
		if hasWAL && hasData && !hasMeta {
			return FaultBdevConfigRolesWalDataNoMeta
		}

		if hasWAL {
			wal++
		}
		if hasMeta {
			meta++
		}
		if hasData {
			data++
		}
	}

	if !hasRoles() {
		if len(bcs) > maxNrBdevTiersWithoutRoles {
			return FaultBdevConfigMultiTiersWithoutRoles
		}
		return nil // MD-on-SSD is not to be enabled
	}

	if sc.Class == ClassDcpm {
		return FaultBdevConfigRolesWithDCPM
	} else if sc.Class != ClassRam {
		return errors.Errorf("unexpected scm class %s", sc.Class)
	}

	// MD-on-SSD configurations supports 1, 2 or 3 bdev tiers.
	if len(bcs) > maxNrBdevTiersWithRoles {
		return FaultBdevConfigBadNrTiersWithRoles
	}

	// When roles have been assigned, each role should be seen exactly once.
	if wal != 1 {
		return FaultBdevConfigBadNrRoles("WAL", wal, 1)
	}
	if meta != 1 {
		return FaultBdevConfigBadNrRoles("Meta", meta, 1)
	}
	if data != 1 {
		return FaultBdevConfigBadNrRoles("Data", data, 1)
	}

	return nil
}

// Set NVME class tier roles either based on explicit settings or heuristics.
//
// Role assignments will be decided based on the following rule set:
//   - For 1 bdev tier, all roles will be assigned to that tier.
//   - For 2 bdev tiers, WAL role will be assigned to the first bdev tier and Meta and Data to
//     the second bdev tier.
//   - For 3 or more bdev tiers, WAL role will be assigned to the first bdev tier, Meta to the
//     second bdev tier and Data to all remaining bdev tiers.
//   - If the scm tier is of class dcpm, the first (and only) bdev tier should have the Data role.
//   - If emulated NVMe is present in bdev tiers, implicit role assignment is skipped.
func (tcs TierConfigs) AssignBdevTierRoles() error {
	scs := tcs.ScmConfigs()

	// Require tier-0 to be a SCM tier.
	if len(scs) != 1 || scs[0].Tier != 0 {
		return errors.New("first storage tier is not scm")
	}
	// No roles should be assigned if scm tier is DCPM.
	if scs[0].Class == ClassDcpm {
		return nil
	}
	// Skip role assignment and validation if no real NVMe tiers exist.
	if !tcs.HaveRealNVMe() {
		return nil
	}

	bcs := tcs.BdevConfigs()

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
		return nil
	case l == len(bcs):
		// No assigned roles, fall-through to perform implicit assignment.
	default:
		// Some bdev tiers have assigned roles but not all, unsupported.
		return FaultBdevConfigRolesMissing
	}

	// Apply role assignments.
	switch len(bcs) {
	case 1:
		tcs[1].WithBdevDeviceRoles(BdevRoleAll)
	case 2:
		tcs[1].WithBdevDeviceRoles(BdevRoleWAL)
		tcs[2].WithBdevDeviceRoles(BdevRoleMeta | BdevRoleData)
	default:
		tcs[1].WithBdevDeviceRoles(BdevRoleWAL)
		tcs[2].WithBdevDeviceRoles(BdevRoleMeta)
		for i := 3; i < len(tcs); i++ {
			tcs[i].WithBdevDeviceRoles(BdevRoleData)
		}
	}

	return nil
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

	tid := 0
	tmp2 := make([]*TierConfig, 0, len(tmp))
	for i := range tmp {
		if tmp[i] == nil {
			continue
		}
		tmp[i].Tier = tid
		tmp2 = append(tmp2, tmp[i])
		tid++
	}
	*tcs = tmp2

	return nil
}

// ScmConfig represents a SCM (Storage Class Memory) configuration entry.
type ScmConfig struct {
	MountPoint       string   `yaml:"scm_mount,omitempty" cmdLongFlag:"--storage" cmdShortFlag:"-s"`
	RamdiskSize      uint     `yaml:"scm_size,omitempty"`
	DisableHugepages bool     `yaml:"scm_hugepages_disabled,omitempty"`
	DeviceList       []string `yaml:"scm_list,omitempty"`
	NumaNodeIndex    uint     `yaml:"-"`
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
		if sc.DisableHugepages {
			return errors.New("scm_hugepages_disabled may not be set when scm_class is dcpm")
		}
	case ClassRam:
		if len(sc.DeviceList) > 0 {
			return errors.New("scm_list may not be set when scm_class is ram")
		}
		// Note: RAM-disk size can be auto-sized so allow if zero.
		if sc.RamdiskSize != 0 {
			confScmSize := uint64(humanize.GiByte * sc.RamdiskSize)
			if confScmSize < MinRamdiskMem {
				// Ramdisk size requested in config is less than minimum allowed.
				return FaultConfigRamdiskUnderMinMem(confScmSize, MinRamdiskMem)
			}
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

// toString returns a comma separated list of option names that have been set.
func (obs OptionBits) toString(optStr2Flag optFlagMap) string {
	return strings.Join(obs.toStrings(optStr2Flag), ",")
}

// fromStrings generates bitset referenced by the function receiver from the option names provided.
func (obs *OptionBits) fromStrings(optStr2Flag optFlagMap, opts ...string) error {
	if obs == nil {
		return errors.New("called on nil OptionBits")
	}

	*obs = 0
	for _, opt := range opts {
		if len(opt) == 0 {
			continue
		}
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

// BdevRoles is a bitset representing SSD role assignments (enabling Metadata-on-SSD).
type BdevRoles struct {
	OptionBits
}

func (bdr BdevRoles) MarshalYAML() (interface{}, error) {
	return bdr.toStrings(roleOptFlags), nil
}

func (bdr *BdevRoles) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var opts []string
	if err := unmarshal(&opts); err != nil {
		return err
	}

	return bdr.fromStrings(roleOptFlags, opts...)
}

// MarshalJSON represents roles as user readable string.
func (bdr BdevRoles) MarshalJSON() ([]byte, error) {
	return []byte(`"` + bdr.String() + `"`), nil
}

// UnmarshalJSON decodes user readable roles string into bitmask.
func (bdr *BdevRoles) UnmarshalJSON(data []byte) error {
	str := strings.Trim(strings.ToLower(string(data)), "\"")
	return bdr.fromStrings(roleOptFlags, strings.Split(str, ",")...)
}

func (bdr *BdevRoles) String() string {
	if bdr == nil {
		return "none"
	}
	return bdr.toString(roleOptFlags)
}

// BdevConfig represents a Block Device (NVMe, etc.) configuration entry.
type BdevConfig struct {
	DeviceList    *BdevDeviceList `yaml:"bdev_list,omitempty"`
	DeviceCount   int             `yaml:"bdev_number,omitempty"`
	FileSize      int             `yaml:"bdev_size,omitempty"`
	BusidRange    *BdevBusRange   `yaml:"bdev_busid_range,omitempty"`
	DeviceRoles   BdevRoles       `yaml:"bdev_roles,omitempty"`
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

// SpdkRpcServer struct describes settings for an optional SPDK JSON-RPC server instance that can
// run in the engine process.
type SpdkRpcServer struct {
	Enable   bool   `yaml:"enable,omitempty" json:"enable"`
	SockAddr string `yaml:"sock_addr,omitempty" json:"sock_addr"`
}

type Config struct {
	ControlMetadata  ControlMetadata `yaml:"-"` // inherited from server
	EngineIdx        uint            `yaml:"-"`
	Tiers            TierConfigs     `yaml:"storage" cmdLongFlag:"--storage_tiers,nonzero" cmdShortFlag:"-T,nonzero"`
	ConfigOutputPath string          `yaml:"-" cmdLongFlag:"--nvme" cmdShortFlag:"-n"`
	VosEnv           string          `yaml:"-" cmdEnv:"VOS_BDEV_CLASS"`
	EnableHotplug    bool            `yaml:"-"`
	NumaNodeIndex    uint            `yaml:"-"`
	AccelProps       AccelProps      `yaml:"acceleration,omitempty"`
	SpdkRpcSrvProps  SpdkRpcServer   `yaml:"spdk_rpc_server,omitempty"`
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
		return err
	}

	bdevCfgs := c.Tiers.BdevConfigs()

	// set persistent location for engine bdev config file to be consumed by provider
	// backend, set to empty when no devices specified
	if len(bdevCfgs) == 0 {
		c.ConfigOutputPath = ""
		if c.ControlMetadata.HasPath() {
			return FaultBdevConfigControlMetadataNoRoles
		}

		return nil
	}

	// set vos environment variable based on class of first bdev config
	switch bdevCfgs[0].Class {
	case ClassNvme:
		c.VosEnv = "NVME"
	case ClassFile, ClassKdev:
		c.VosEnv = "AIO"
	}

	var nvmeConfigRoot string
	if c.ControlMetadata.HasPath() {
		if !c.Tiers.HasBdevRoleMeta() {
			return FaultBdevConfigControlMetadataNoRoles
		}
		nvmeConfigRoot = c.ControlMetadata.EngineDirectory(c.EngineIdx)
	} else {
		if c.Tiers.HasBdevRoleMeta() {
			return FaultBdevConfigRolesNoControlMetadata
		}
		nvmeConfigRoot = c.Tiers.ScmConfigs()[0].Scm.MountPoint
	}
	c.ConfigOutputPath = filepath.Join(nvmeConfigRoot, BdevOutConfName)

	return nil
}
