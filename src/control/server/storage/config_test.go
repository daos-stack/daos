//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"encoding/json"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
)

func defConfigCmpOpts() cmp.Options {
	return cmp.Options{
		cmp.Comparer(func(x, y *BdevDeviceList) bool {
			if x == nil && y == nil {
				return true
			}
			return x.Equals(y)
		}),
	}
}

func TestStorage_BdevDeviceList_Devices(t *testing.T) {
	for name, tc := range map[string]struct {
		list      *BdevDeviceList
		expResult []string
	}{
		"nil": {
			expResult: []string{},
		},
		"empty": {
			list:      &BdevDeviceList{},
			expResult: []string{},
		},
		"string set": {
			list: &BdevDeviceList{
				stringBdevSet: common.NewStringSet("one", "two"),
			},
			expResult: []string{"one", "two"},
		},
		"PCI addresses": {
			list: &BdevDeviceList{
				PCIAddressSet: *hardware.MustNewPCIAddressSet(
					"0000:01:01.0",
					"0000:02:02.0",
				),
			},
			expResult: []string{"0000:01:01.0", "0000:02:02.0"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.list.Devices()

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_BdevDeviceList(t *testing.T) {
	for name, tc := range map[string]struct {
		devices    []string
		expList    *BdevDeviceList
		expYamlStr string
		expJSONStr string
		expErr     error
	}{
		"empty": {
			expList:    &BdevDeviceList{},
			expYamlStr: "[]\n",
			expJSONStr: "[]",
		},
		"valid pci addresses": {
			devices: []string{"0000:81:00.0", "0000:82:00.0"},
			expList: &BdevDeviceList{
				PCIAddressSet: func() hardware.PCIAddressSet {
					set, err := hardware.NewPCIAddressSetFromString("0000:81:00.0 0000:82:00.0")
					if err != nil {
						panic(err)
					}
					return *set
				}(),
			},
			expYamlStr: `
- 0000:81:00.0
- 0000:82:00.0
`,
			expJSONStr: `["0000:81:00.0","0000:82:00.0"]`,
		},
		"non-pci devices": {
			devices: []string{"/dev/block0", "/dev/block1"},
			expList: &BdevDeviceList{
				stringBdevSet: common.NewStringSet("/dev/block0", "/dev/block1"),
			},
			expYamlStr: `
- /dev/block0
- /dev/block1
`,
			expJSONStr: `["/dev/block0","/dev/block1"]`,
		},
		"invalid pci device": {
			devices: []string{"0000:8g:00.0"},
			expErr:  errors.New("unable to parse \"0000:8g:00.0\""),
		},
		"mixed pci and non-pci devices": {
			devices: []string{"/dev/block0", "0000:81:00.0"},
			expErr:  errors.New("mix"),
		},
		"duplicate pci device": {
			devices: []string{"0000:81:00.0", "0000:81:00.0"},
			expErr:  errors.New("duplicate"),
		},
		"duplicate non-pci device": {
			devices: []string{"/dev/block0", "/dev/block0"},
			expErr:  errors.New("duplicate"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			list, err := NewBdevDeviceList(tc.devices...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expList, list, defConfigCmpOpts()...); diff != "" {
				t.Fatalf("bad list (-want +got):\n%s", diff)
			}

			yamlData, err := yaml.Marshal(list)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(strings.TrimLeft(tc.expYamlStr, "\n"), string(yamlData)); diff != "" {
				t.Fatalf("bad yaml (-want +got):\n%s", diff)
			}

			jsonData, err := json.Marshal(list)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(strings.TrimLeft(tc.expJSONStr, "\n"), string(jsonData)); diff != "" {
				t.Fatalf("bad json (-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_BdevDeviceList_FromYAML(t *testing.T) {
	for name, tc := range map[string]struct {
		input   string
		expList *BdevDeviceList
		expErr  error
	}{
		"empty": {
			input:   "[]\n",
			expList: &BdevDeviceList{},
		},
		"valid pci addresses": {
			input: `["0000:81:00.0","0000:82:00.0"]`,
			expList: &BdevDeviceList{
				PCIAddressSet: func() hardware.PCIAddressSet {
					set, err := hardware.NewPCIAddressSetFromString("0000:81:00.0 0000:82:00.0")
					if err != nil {
						panic(err)
					}
					return *set
				}(),
			},
		},
		"non-pci devices": {
			input: `
- /dev/block0
- /dev/block1
`,
			expList: &BdevDeviceList{
				stringBdevSet: common.NewStringSet("/dev/block0", "/dev/block1"),
			},
		},
		"mixed pci and non-pci devices": {
			input:  `["/dev/block0", "0000:81:00.0"]`,
			expErr: errors.New("mix"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			list := &BdevDeviceList{}
			err := yaml.UnmarshalStrict([]byte(tc.input), list)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expList, list, defConfigCmpOpts()...); diff != "" {
				t.Fatalf("bad list (-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_BdevDeviceList_FromJSON(t *testing.T) {
	for name, tc := range map[string]struct {
		input   string
		expList *BdevDeviceList
		expErr  error
	}{
		"empty": {
			input:   "[]\n",
			expList: &BdevDeviceList{},
		},
		"valid pci addresses": {
			input: `["0000:81:00.0","0000:82:00.0"]`,
			expList: &BdevDeviceList{
				PCIAddressSet: func() hardware.PCIAddressSet {
					set, err := hardware.NewPCIAddressSetFromString("0000:81:00.0 0000:82:00.0")
					if err != nil {
						panic(err)
					}
					return *set
				}(),
			},
		},
		"non-pci devices": {
			input: `["/dev/block0","/dev/block1"]`,
			expList: &BdevDeviceList{
				stringBdevSet: common.NewStringSet("/dev/block0", "/dev/block1"),
			},
		},
		"mixed pci and non-pci devices": {
			input:  `["/dev/block0", "0000:81:00.0"]`,
			expErr: errors.New("mix"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			list := &BdevDeviceList{}
			err := json.Unmarshal([]byte(tc.input), list)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expList, list, defConfigCmpOpts()...); diff != "" {
				t.Fatalf("bad list (-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_parsePCIBusRange(t *testing.T) {
	for name, tc := range map[string]struct {
		rangeStr string
		bitSize  int
		expBegin uint8
		expEnd   uint8
		expErr   error
	}{
		"hexadecimal": {
			rangeStr: "0x80-0x8f",
			expBegin: 0x80,
			expEnd:   0x8f,
		},
		"incorrect hexadecimal": {
			rangeStr: "0x8g-0x8f",
			expErr:   errors.New("parsing \"0x8g\""),
		},
		"hexadecimal upper": {
			rangeStr: "0x80-0x8F",
			expBegin: 0x80,
			expEnd:   0x8F,
		},
		"decimal": {
			rangeStr: "128-143",
			expBegin: 0x80,
			expEnd:   0x8F,
		},
		"bad range": {
			rangeStr: "128-143-0",
			expErr:   errors.New("invalid busid range \"128-143-0\""),
		},
		"reverse range": {
			rangeStr: "143-0",
			expErr:   errors.New("invalid busid range \"143-0\""),
		},
		"bad separator": {
			rangeStr: "143:0",
			expErr:   errors.New("invalid busid range \"143:0\""),
		},
		"hexadecimal; no prefix": {
			rangeStr: "00-5d",
			bitSize:  8,
			expErr:   errors.New("invalid syntax"),
		},
		"hexadecimal; bit-size exceeded": {
			rangeStr: "0x000-0x5dd",
			bitSize:  8,
			expErr:   errors.New("value out of range"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			begin, end, err := parsePCIBusRange(tc.rangeStr, tc.bitSize)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expBegin, begin, "bad beginning limit")
			test.AssertEqual(t, tc.expEnd, end, "bad ending limit")
		})
	}
}

func TestStorage_TierConfigs_Validate(t *testing.T) {
	for name, tc := range map[string]struct {
		input           string
		expTierCfgs     TierConfigs
		expUnmarshalErr error
		expValidateErr  error
	}{
		"section missing": {
			input:          ``,
			expValidateErr: errors.New("no storage tiers"),
		},
		"tier class missing": {
			input: `
storage:
-
  scm_size: 16
  scm_mount: /mnt/daos`,
			expValidateErr: FaultScmConfigTierMissing,
		},
		"scm tier missing": {
			input: `
storage:
-
  class: nvme
  bdev_list: [0000:80:00.0]`,
			expValidateErr: FaultScmConfigTierMissing,
		},
		"mixed bdev tier types": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
-
  class: file
  bdev_list: [/tmp/daos0.aio]`,
			expValidateErr: FaultBdevConfigTierTypeMismatch,
		},
		"tier 1 fails validation": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80a:00.0]`,
			expValidateErr: errors.New("tier 1 failed validation"),
		},
		"roles specified; ram scm tier; second bdev tier with unassigned roles": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [wal]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]`,
			expValidateErr: FaultBdevConfigRolesMissing,
		},
		"roles specified; ram scm tier; first bdev tier with unassigned roles": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
  bdev_roles: [wal,meta,data]`,
			expValidateErr: FaultBdevConfigRolesMissing,
		},
		"roles unspecified; ram scm tier; one bdev tier": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]`,
			expTierCfgs: TierConfigs{
				NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(16).
					WithScmMountPoint("/mnt/daos"),
				NewTierConfig().
					WithTier(1).
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:80:00.0"),
			},
		},
		"roles unspecified; ram scm tier; two bdev tiers": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]`,
			expValidateErr: FaultBdevConfigMultiTiersWithoutRoles,
		},
		"roles unspecified; dcpm scm tier; three bdev tiers": {
			input: `
storage:
-
  class: dcpm
  scm_list: [/dev/pmem0]
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
-
  class: nvme
  bdev_list: [0000:83:00.0,0000:84:00.0]`,

			expValidateErr: FaultBdevConfigMultiTiersWithoutRoles,
		},
		"roles specified; dcpm scm tier; three bdev tiers": {
			input: `
storage:
-
  class: dcpm
  scm_list: [/dev/pmem0]
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [wal]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
  bdev_roles: [meta]
-
  class: nvme
  bdev_list: [0000:83:00.0,0000:84:00.0]
  bdev_roles: [data]`,

			expValidateErr: FaultBdevConfigRolesWithDCPM,
		},
		"roles specified; ram scm class; four bdev tiers": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [wal]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
  bdev_roles: [meta]
-
  class: nvme
  bdev_list: [0000:83:00.0,0000:84:00.0]
  bdev_roles: [data]
-
  class: nvme
  bdev_list: [0000:85:00.0,0000:86:00.0]
  bdev_roles: [data]`,
			expValidateErr: FaultBdevConfigBadNrTiersWithRoles,
		},
		"roles specified; ram scm class": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [meta,wal]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
  bdev_roles: [data]`,
			expTierCfgs: TierConfigs{
				NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(16).
					WithScmMountPoint("/mnt/daos"),
				NewTierConfig().
					WithTier(1).
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:80:00.0").
					WithBdevDeviceRoles(BdevRoleMeta | BdevRoleWAL),
				NewTierConfig().
					WithTier(2).
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
					WithBdevDeviceRoles(BdevRoleData),
			},
		},
		"roles specified; ram scm class; alternative yaml format": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list:
  - 0000:80:00.0
  bdev_roles:
  - meta
  - wal
-
  class: nvme
  bdev_list:
  - 0000:81:00.0
  - 0000:82:00.0
  bdev_roles:
  - data`,
			expTierCfgs: TierConfigs{
				NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(16).
					WithScmMountPoint("/mnt/daos"),
				NewTierConfig().
					WithTier(1).
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:80:00.0").
					WithBdevDeviceRoles(BdevRoleMeta | BdevRoleWAL),
				NewTierConfig().
					WithTier(2).
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
					WithBdevDeviceRoles(BdevRoleData),
			},
		},
		"roles specified; ram scm class; unrecognized role": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [foobar]`,
			expUnmarshalErr: errors.New("unknown option flag"),
		},
		"roles specified; dcpm scm class; illegal roles": {
			input: `
storage:
-
  class: dcpm
  scm_list: [/dev/pmem0]
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [meta,wal,data]`,
			expValidateErr: FaultBdevConfigRolesWithDCPM,
		},
		"roles specified; ram scm tier; illegal wal+data combination": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [meta]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
  bdev_roles: [wal,data]`,
			expValidateErr: FaultBdevConfigRolesWalDataNoMeta,
		},
		"roles specified; ram scm tier; duplicate wal roles": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [meta,wal,data]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
  bdev_roles: [wal]`,
			expValidateErr: FaultBdevConfigBadNrRoles("WAL", 2, 1),
		},
		"roles specified; ram scm tier; duplicate meta roles": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [meta,wal,data]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
  bdev_roles: [meta]`,
			expValidateErr: FaultBdevConfigBadNrRoles("Meta", 2, 1),
		},
		"roles specified; ram scm tier; duplicate data roles": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [data,wal,meta]
-
  class: nvme
  bdev_list: [0000:81:00.0,0000:82:00.0]
  bdev_roles: [data]`,
			expValidateErr: FaultBdevConfigBadNrRoles("Data", 2, 1),
		},
		"roles specified; ram scm tier; missing data role": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list: [0000:80:00.0]
  bdev_roles: [meta,wal]`,
			expValidateErr: FaultBdevConfigBadNrRoles("Data", 0, 1),
		},
		"roles specified; ram scm tier; only data role assigned": {
			input: `
storage:
-
  class: ram
  scm_size: 16
  scm_mount: /mnt/daos
-
  class: nvme
  bdev_list:
  - 0000:80:00.0
  bdev_roles:
  - data`,
			expValidateErr: FaultBdevConfigBadNrRoles("WAL", 0, 1),
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := new(Config)
			err := yaml.UnmarshalStrict([]byte(tc.input), cfg)
			test.CmpErr(t, tc.expUnmarshalErr, err)
			if err != nil {
				return
			}

			err = cfg.Tiers.Validate()
			test.CmpErr(t, tc.expValidateErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expTierCfgs, cfg.Tiers, defConfigCmpOpts()...); diff != "" {
				t.Fatalf("bad roles (-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_BdevDeviceRoles_ToYAML(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg    *Config
		expOut string
		expErr error
	}{
		"section missing": {
			cfg:    &Config{},
			expOut: "storage: []\n",
		},
		"roles specified": {
			cfg: &Config{
				Tiers: TierConfigs{
					NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(16).
						WithScmMountPoint("/mnt/daos"),
					NewTierConfig().
						WithTier(1).
						WithStorageClass("nvme").
						WithBdevDeviceList("0000:80:00.0").
						WithBdevDeviceRoles(BdevRoleAll),
				},
			},
			expOut: `
storage:
- class: ram
  scm_mount: /mnt/daos
  scm_size: 16
- class: nvme
  bdev_list:
  - 0000:80:00.0
  bdev_roles:
  - data
  - meta
  - wal
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			bytes, err := yaml.Marshal(tc.cfg)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expOut, "\n"), string(bytes),
				defConfigCmpOpts()...); diff != "" {
				t.Fatalf("bad yaml output (-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_AccelProps_FromYAML(t *testing.T) {
	for name, tc := range map[string]struct {
		input    string
		expProps AccelProps
		expErr   error
	}{
		"acceleration section missing": {
			input: ``,
		},
		"acceleration section empty": {
			input: `
acceleration:
`,
		},
		"engine unset": {
			input: `
acceleration:
  engine:
`,
			expErr: errors.New("unknown acceleration engine"),
		},
		"engine set empty": {
			input: `
acceleration:
  engine: ""
`,
			expErr: errors.New("unknown acceleration engine"),
		},
		"engine set; default opts": {
			input: `
acceleration:
  engine: spdk
`,
			expProps: AccelProps{
				Engine:  AccelEngineSPDK,
				Options: AccelOptCRCFlag | AccelOptMoveFlag,
			},
		},
		"engine unset; opts set": {
			input: `
acceleration:
  options:
  - move
  - crc
`,
			expProps: AccelProps{
				Engine: AccelEngineNone,
			},
		},
		"engine set; opts set": {
			input: `
acceleration:
  engine: dml
  options:
  - crc
`,
			expProps: AccelProps{
				Engine:  AccelEngineDML,
				Options: AccelOptCRCFlag,
			},
		},
		"engine set; opts all set": {
			input: `
acceleration:
  engine: spdk
  options:
  - crc
  - move
`,
			expProps: AccelProps{
				Engine:  AccelEngineSPDK,
				Options: AccelOptCRCFlag | AccelOptMoveFlag,
			},
		},
		"unrecognized engine": {
			input: `
acceleration:
  engine: native
`,
			expErr: errors.New("unknown acceleration engine"),
		},
		"unrecognized option": {
			input: `
acceleration:
  engine: dml
  options:
  - bar
`,
			expErr: errors.New("unknown option flag"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := new(Config)
			err := yaml.UnmarshalStrict([]byte(tc.input), cfg)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expProps, cfg.AccelProps, defConfigCmpOpts()...); diff != "" {
				t.Fatalf("bad props (-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_AccelProps_ToYAML(t *testing.T) {
	for name, tc := range map[string]struct {
		props  AccelProps
		expOut string
		expErr error
	}{
		"nil props": {
			expOut: `
storage: []
`,
		},
		"empty props": {
			expOut: `
storage: []
`,
		},
		"engine set": {
			props: AccelProps{Engine: AccelEngineNone},
			expOut: `
storage: []
acceleration:
  engine: none
`,
		},
		"engine set; default opts": {
			props: AccelProps{
				Engine:  AccelEngineSPDK,
				Options: AccelOptCRCFlag | AccelOptMoveFlag,
			},
			expOut: `
storage: []
acceleration:
  engine: spdk
  options:
  - crc
  - move
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := &Config{
				AccelProps: tc.props,
			}

			bytes, err := yaml.Marshal(cfg)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expOut, "\n"), string(bytes), defConfigCmpOpts()...); diff != "" {
				t.Fatalf("bad yaml output (-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_ControlMetadata_Directory(t *testing.T) {
	for name, tc := range map[string]struct {
		cm        ControlMetadata
		expResult string
	}{
		"empty": {},
		"path": {
			cm: ControlMetadata{
				Path: "/some/thing",
			},
			expResult: "/some/thing/daos_control",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.cm.Directory(), "")
		})
	}
}

func TestStorage_ControlMetadata_EngineDirectory(t *testing.T) {
	for name, tc := range map[string]struct {
		cm        ControlMetadata
		idx       uint
		expResult string
	}{
		"empty": {},
		"path": {
			cm: ControlMetadata{
				Path: "/some/thing",
			},
			idx:       123,
			expResult: "/some/thing/daos_control/engine123",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.cm.EngineDirectory(tc.idx), "")
		})
	}
}

func TestStorage_ControlMetadata_HasPath(t *testing.T) {
	for name, tc := range map[string]struct {
		cm        ControlMetadata
		expResult bool
	}{
		"empty": {},
		"path": {
			cm: ControlMetadata{
				Path: "/some/thing",
			},
			expResult: true,
		},
		"path and device": {
			cm: ControlMetadata{
				Path:       "/some/thing",
				DevicePath: "/other/thing",
			},
			expResult: true,
		},
		"device only": {
			cm: ControlMetadata{
				DevicePath: "/some/thing",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.cm.HasPath(), "")
		})
	}
}

func TestStorage_TierConfigs_FromYAML(t *testing.T) {
	for name, tc := range map[string]struct {
		input    string
		expTiers TierConfigs
		expErr   error
	}{
		"single tier": {
			input: `
storage:
-
  class: ram
  scm_mount: /mnt/daos/1
  scm_size: 16
`,
			expTiers: TierConfigs{
				&TierConfig{
					Class: ClassRam,
					Scm: ScmConfig{
						MountPoint:  "/mnt/daos/1",
						RamdiskSize: 16,
					},
				},
			},
		},
		"empty tier first": {
			input: `
storage:
-
-
  class: ram
  scm_mount: /mnt/daos/1
  scm_size: 16
-
  class: dcpm
`,
			expTiers: TierConfigs{
				&TierConfig{
					Class: ClassRam,
					Scm: ScmConfig{
						MountPoint:  "/mnt/daos/1",
						RamdiskSize: 16,
					},
				},
				&TierConfig{
					Tier:  1,
					Class: ClassDcpm,
				},
			},
		},
		"empty tier middle": {
			input: `
storage:
-
  class: ram
  scm_mount: /mnt/daos/1
  scm_size: 16
-
-
  class: dcpm
`,
			expTiers: TierConfigs{
				&TierConfig{
					Class: ClassRam,
					Scm: ScmConfig{
						MountPoint:  "/mnt/daos/1",
						RamdiskSize: 16,
					},
				},
				&TierConfig{
					Tier:  1,
					Class: ClassDcpm,
				},
			},
		},
		"empty tier last": {
			input: `
storage:
-
  class: ram
  scm_mount: /mnt/daos/1
  scm_size: 16
-
  class: dcpm
-
`,
			expTiers: TierConfigs{
				&TierConfig{
					Class: ClassRam,
					Scm: ScmConfig{
						MountPoint:  "/mnt/daos/1",
						RamdiskSize: 16,
					},
				},
				&TierConfig{
					Tier:  1,
					Class: ClassDcpm,
				},
			},
		},
		"two empty tiers last": {
			input: `
storage:
-
  class: ram
  scm_mount: /mnt/daos/1
  scm_size: 16
-
  class: dcpm
-
-
`,
			expTiers: TierConfigs{
				&TierConfig{
					Class: ClassRam,
					Scm: ScmConfig{
						MountPoint:  "/mnt/daos/1",
						RamdiskSize: 16,
					},
				},
				&TierConfig{
					Tier:  1,
					Class: ClassDcpm,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := new(Config)
			err := yaml.UnmarshalStrict([]byte(tc.input), cfg)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expTiers, cfg.Tiers, defConfigCmpOpts()...); diff != "" {
				t.Fatalf("bad props (-want +got):\n%s", diff)
			}
		})
	}
}

func TestStorage_Config_Validate(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg                 Config
		expConfigOutputPath string
		expVosEnv           string
		expErr              error
	}{
		"tiers fail validation": {
			expErr: errors.New("no storage tiers"),
		},
		"roles configured but no control_metadata": {
			cfg: Config{
				Tiers: TierConfigs{
					NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(16).
						WithScmMountPoint("/mnt/daos"),
					NewTierConfig().
						WithTier(1).
						WithStorageClass("nvme").
						WithBdevDeviceList("0000:80:00.0").
						WithBdevDeviceRoles(BdevRoleMeta | BdevRoleWAL),
					NewTierConfig().
						WithTier(2).
						WithStorageClass("nvme").
						WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
						WithBdevDeviceRoles(BdevRoleData),
				},
			},
			expErr: FaultBdevConfigRolesNoControlMetadata,
		},
		"no bdevs configured but control_metadata path specified": {
			cfg: Config{
				ControlMetadata: ControlMetadata{
					Path: "/",
				},
				Tiers: TierConfigs{
					NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(16).
						WithScmMountPoint("/mnt/daos"),
				},
			},
			expErr: FaultBdevConfigControlMetadataNoRoles,
		},
		"no roles configured but control_metadata path specified": {
			cfg: Config{
				ControlMetadata: ControlMetadata{
					Path: "/",
				},
				Tiers: TierConfigs{
					NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(16).
						WithScmMountPoint("/mnt/daos"),
					NewTierConfig().
						WithTier(1).
						WithStorageClass("nvme").
						WithBdevDeviceList("0000:80:00.0"),
				},
			},
			expErr: FaultBdevConfigControlMetadataNoRoles,
		},
		"roles configured with control_metadata path": {
			cfg: Config{
				ControlMetadata: ControlMetadata{
					Path: "/",
				},
				Tiers: TierConfigs{
					NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(16).
						WithScmMountPoint("/mnt/daos"),
					NewTierConfig().
						WithTier(1).
						WithStorageClass("nvme").
						WithBdevDeviceList("0000:80:00.0").
						WithBdevDeviceRoles(BdevRoleMeta | BdevRoleWAL),
					NewTierConfig().
						WithTier(2).
						WithStorageClass("nvme").
						WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
						WithBdevDeviceRoles(BdevRoleData),
				},
			},
			expVosEnv:           "NVME",
			expConfigOutputPath: "/daos_control/engine0/daos_nvme.conf",
		},
		"no bdevs without control_metadata path": {
			cfg: Config{
				Tiers: TierConfigs{
					NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(16).
						WithScmMountPoint("/mnt/daos"),
				},
			},
		},
		"roles configured with control_metadata path and emulated nvme": {
			cfg: Config{
				ControlMetadata: ControlMetadata{
					Path: "/",
				},
				Tiers: TierConfigs{
					NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(16).
						WithScmMountPoint("/mnt/daos"),
					NewTierConfig().
						WithTier(1).
						WithStorageClass("file").
						WithBdevDeviceList("/tmp/daos0.aio").
						WithBdevFileSize(16).
						WithBdevDeviceRoles(BdevRoleAll),
				},
			},
			expVosEnv:           "AIO",
			expConfigOutputPath: "/daos_control/engine0/daos_nvme.conf",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, tc.cfg.Validate())
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expVosEnv, tc.cfg.VosEnv); diff != "" {
				t.Fatalf("unexpected VosEnv (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.expConfigOutputPath, tc.cfg.ConfigOutputPath); diff != "" {
				t.Fatalf("unexpected ConfigOutputPath (-want +got):\n%s", diff)
			}
		})
	}
}
