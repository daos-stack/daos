//
// (C) Copyright 2019-2022 Intel Corporation.
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

const (
	mockAccelEngineNone    = "none"
	mockAccelEngineDML     = "dml"
	mockAccelOptMaskCRCSet = 0b10
)

var (
	mockAccelOptsAllSet = []string{"move", "crc"}
	mockAccelOptsCRCSet = []string{"crc"}
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
			err := yaml.Unmarshal([]byte(tc.input), list)
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

func TestStorage_AccelProps_FromYAML(t *testing.T) {
	for name, tc := range map[string]struct {
		input    string
		expProps AccelProps
		expErr   error
	}{
		"acceleration section missing": {
			input:    "",
			expProps: AccelProps{},
		},
		"acceleration section empty": {
			input:    "acceleration:\n",
			expProps: AccelProps{},
		},
		"engine unset": {
			input: "acceleration:\n  engine:\n",
			expProps: AccelProps{
				AccelEngine: mockAccelEngineNone,
			},
		},
		"engine set empty": {
			input: "acceleration:\n  engine: \"\"\n",
			expProps: AccelProps{
				AccelEngine: mockAccelEngineNone,
			},
		},
		"set engine": {
			input: "acceleration:\n  engine: \"spdk\"\n",
			expProps: AccelProps{
				AccelEngine:  mockAccelEngineSPDK,
				AccelOpts:    mockAccelOptsAllSet,
				AccelOptMask: mockAccelOptMaskAllSet,
			},
		},
		"set options; missing engine": {
			input: "acceleration:\n  options:\n    - move\n    - crc\n",
			expProps: AccelProps{
				AccelEngine: mockAccelEngineNone,
			},
		},
		"set engine; set options": {
			input: "acceleration:\n  engine: \"dml\"\n  options:\n    - crc\n",
			expProps: AccelProps{
				AccelEngine:  mockAccelEngineDML,
				AccelOpts:    mockAccelOptsCRCSet,
				AccelOptMask: mockAccelOptMaskCRCSet,
			},
		},
		"unrecognized engine": {
			input:  "acceleration:\n  engine: \"foo\"\n",
			expErr: errors.New("unknown acceleration engine"),
		},
		"unrecognized option": {
			input:  "acceleration:\n  engine: \"dml\"\n  options:\n    - bar\n",
			expErr: errors.New("unknown acceleration option"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			cfg := new(Config)
			err := yaml.Unmarshal([]byte(tc.input), cfg)
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
