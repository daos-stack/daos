//
// (C) Copyright 2021-2022 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestDaos_ContainerProperty_Label(t *testing.T) {
	for name, tc := range map[string]struct {
		input  string
		expErr error
	}{
		"empty": {
			input:  "",
			expErr: errors.New("invalid label"),
		},
		"invalid label": {
			input:  uuid.New().String(),
			expErr: errors.New("invalid label"),
		},
		"valid label": {
			input:  "good-label",
			expErr: nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testContainerPropertyInput(t, ContainerPropLabel, tc.input, tc.expErr)
		})
	}
}

func TestDaos_ContainerProperty_Checksum(t *testing.T) {
	testProp := newTestContainerProperty(ContainerPropChecksumEnabled)
	testContainerPropertyInputs(t, testProp.Type, testProp.hdlr.valHdlrs.keys())
}

func TestDaos_ContainerProperty_ChecksumSize(t *testing.T) {
	// Negative tests
	testContainerPropertyInputs(t, ContainerPropChecksumSize, nil)

	for name, tc := range map[string]struct {
		input    string
		expBytes uint64
		expErr   error
	}{
		"Human Entry": {
			input:    "4 KiB",
			expBytes: 4096,
		},
		"Bytes Entry": {
			input:    strconv.FormatUint(123456, 10),
			expBytes: 123456,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testContainerPropertyInput(t, ContainerPropChecksumSize, tc.input, tc.expErr, humanize.IBytes(tc.expBytes))
		})
	}
}

func TestDaos_ContainerProperty_ServerChecksumEnabled(t *testing.T) {
	testProp := newTestContainerProperty(ContainerPropChecksumSrvVrfy)
	testContainerPropertyInputs(t, testProp.Type, testProp.hdlr.valHdlrs.keys())
}

func TestDaos_ContainerProperty_Deduplication(t *testing.T) {
	testProp := newTestContainerProperty(ContainerPropDedupEnabled)
	testContainerPropertyInputs(t, testProp.Type, testProp.hdlr.valHdlrs.keys())
}

func TestDaos_ContainerProperty_DedupeThreshold(t *testing.T) {
	// Negative tests
	testContainerPropertyInputs(t, ContainerPropDedupThreshold, nil)

	for name, tc := range map[string]struct {
		input    string
		expBytes uint64
		expErr   error
	}{
		"Human Entry": {
			input:    "4 KiB",
			expBytes: 4096,
		},
		"Bytes Entry": {
			input:    strconv.FormatUint(123456, 10),
			expBytes: 123456,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testContainerPropertyInput(t, ContainerPropDedupThreshold, tc.input, tc.expErr, humanize.IBytes(tc.expBytes))
		})
	}
}

func TestDaos_ContainerProperty_Compression(t *testing.T) {
	testProp := newTestContainerProperty(ContainerPropCompression)
	testContainerPropertyInputs(t, testProp.Type, testProp.hdlr.valHdlrs.keys())
}

func TestDaos_ContainerProperty_Encryption(t *testing.T) {
	testProp := newTestContainerProperty(ContainerPropEncryption)
	testContainerPropertyInputs(t, testProp.Type, testProp.hdlr.valHdlrs.keys())
}

func TestDaos_ContainerProperty_Status(t *testing.T) {
	t.Run("invalid status", func(t *testing.T) {
		healthyProp := newTestContainerProperty(ContainerPropStatus)
		test.CmpErr(t, errors.New("invalid choice"), healthyProp.Set("whoops"))
	})
	t.Run("healthy status", func(t *testing.T) {
		healthyProp := newTestContainerProperty(ContainerPropStatus)
		test.CmpErr(t, nil, healthyProp.Set("healthy"))
		test.AssertEqual(t, healthyProp.StringValue(), "HEALTHY", "invalid string value")
	})
	t.Run("unhealthy status", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropStatus)
		testProp.SetValue(1 << 32)
		test.AssertEqual(t, testProp.StringValue(), "UNCLEAN", "invalid string value")
	})
	t.Run("unknown status", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropStatus)
		testProp.SetValue(1 << 33)
		test.AssertEqual(t, testProp.StringValue(), "property \"status\": invalid value 0x200000000", "unexpected string value")
	})
}

func TestDaos_ContainerProperty_EcCellSize(t *testing.T) {
	// Negative tests
	testContainerPropertyInputs(t, ContainerPropEcCellSize, nil)

	for name, tc := range map[string]struct {
		input    string
		expBytes uint64
		expErr   error
	}{
		"Invalid cell size": {
			input:  "10",
			expErr: errors.New("invalid ec_cell_sz 10"),
		},
		"Human Minimal Entry": {
			input:    humanize.IBytes(ECCellMin),
			expBytes: ECCellMin,
		},
		"Human Default Entry": {
			input:    humanize.IBytes(ECCellDefault),
			expBytes: ECCellDefault,
		},
		"Human Maximal Entry": {
			input:    humanize.IBytes(ECCellMax),
			expBytes: ECCellMax,
		},
		"Bytes Minimal Entry": {
			input:    strconv.FormatUint(ECCellMin, 10),
			expBytes: ECCellMin,
		},
		"Bytes Default Entry": {
			input:    strconv.FormatUint(ECCellDefault, 10),
			expBytes: ECCellDefault,
		},
		"Bytes Maximal Entry": {
			input:    strconv.FormatUint(ECCellMax, 10),
			expBytes: ECCellMax,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testContainerPropertyInput(t, ContainerPropEcCellSize, tc.input, tc.expErr, humanize.IBytes(tc.expBytes))
		})
	}

	t.Run("Invalid Entry error message: invalid size", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropEcCellSize)
		test.AssertEqual(t, "invalid size 0", testProp.StringValue(), "Invalid error message")
	})
}

func TestDaos_ContainerProperty_RedunLevel(t *testing.T) {
	t.Run("invalid choice", func(t *testing.T) {
		testContainerPropertyInput(t, ContainerPropRedunLevel, "whoops", errors.New("invalid choice"))
	})

	testProp := newTestContainerProperty(ContainerPropRedunLevel)
	for _, inputKey := range testProp.hdlr.valHdlrs.keys() {
		t.Run(inputKey, func(t *testing.T) {
			var expStr string
			switch inputKey {
			case "1":
				expStr = "rank (1)"
			case "2":
				expStr = "node (2)"
			case "rank":
				expStr = "rank (1)"
			case "node":
				expStr = "node (2)"
			default:
				t.Fatalf("untested key %q", inputKey)
			}
			testContainerPropertyInput(t, testProp.Type, inputKey, nil, expStr)
		})
	}

	t.Run("unexpected level", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropRedunLevel)
		testProp.SetValue(42)
		test.AssertEqual(t, "(42)", testProp.StringValue(), "unexpected string value")
	})
}

func TestDaos_ContainerProperty_RedunFactor(t *testing.T) {
	t.Run("invalid choice", func(t *testing.T) {
		testContainerPropertyInput(t, ContainerPropRedunFactor, "whoops", errors.New("invalid choice"))
	})

	testProp := newTestContainerProperty(ContainerPropRedunFactor)
	for _, inputKey := range testProp.hdlr.valHdlrs.keys() {
		t.Run(inputKey, func(t *testing.T) {
			var expStr string
			switch inputKey {
			case "0":
				expStr = "rd_fac0"
			case "1":
				expStr = "rd_fac1"
			case "2":
				expStr = "rd_fac2"
			case "3":
				expStr = "rd_fac3"
			case "4":
				expStr = "rd_fac4"
			default:
				t.Fatalf("untested key %q", inputKey)
			}
			testContainerPropertyInput(t, testProp.Type, inputKey, nil, expStr)
		})
	}

	t.Run("unexpected level", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropRedunFactor)
		testProp.SetValue(42)
		test.AssertEqual(t, fmt.Sprintf("property %q: invalid value 0x2a", testProp.Name), testProp.StringValue(), "unexpected string value")
	})
}

func TestDaos_ContainerProperty_PerfDomain(t *testing.T) {
	t.Run("invalid choice", func(t *testing.T) {
		testContainerPropertyInput(t, ContainerPropPerfDomain, "whoops", errors.New("invalid choice"))
	})

	testProp := newTestContainerProperty(ContainerPropPerfDomain)
	for _, inputKey := range testProp.hdlr.valHdlrs.keys() {
		t.Run(inputKey, func(t *testing.T) {
			var expStr string
			switch inputKey {
			case "root":
				expStr = "root (255)"
			case "group":
				expStr = "group (200)"
			default:
				t.Fatalf("untested key %q", inputKey)
			}
			testContainerPropertyInput(t, testProp.Type, inputKey, nil, expStr)
		})
	}

	t.Run("unexpected domain", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropPerfDomain)
		testProp.SetValue(42)
		test.AssertEqual(t, "(42)", testProp.StringValue(), "unexpected string value")
	})
}

func TestDaos_ContainerProperty_EcPerfDom(t *testing.T) {
	// Negative tests
	testContainerPropertyInputs(t, ContainerPropEcPerfDom, nil)

	t.Run("invalid PD", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropEcPerfDom)
		test.CmpErr(t, errors.Errorf("invalid %s 0", testProp.Name), testProp.Set("0"))
	})

	t.Run("valid PD", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropEcPerfDom)
		test.CmpErr(t, nil, testProp.Set("1"))
		test.AssertEqual(t, testProp.StringValue(), "1", "unexpected string value")
	})
}

func TestDaos_ContainerProperty_EcPerfDomAff(t *testing.T) {
	// Negative tests
	testContainerPropertyInputs(t, ContainerPropEcPerfDomAff, nil)

	t.Run("invalid PDA", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropEcPerfDomAff)
		test.CmpErr(t, errors.Errorf("invalid %s 0", testProp.Name), testProp.Set("0"))
	})

	t.Run("valid PDA", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropEcPerfDomAff)
		test.CmpErr(t, nil, testProp.Set("1"))
		test.AssertEqual(t, testProp.StringValue(), "1", "unexpected string value")
	})
}

func testReadOnlyContainerProperty(t *testing.T, propType ContainerPropType) {
	t.Helper()

	testProp := newTestContainerProperty(propType)
	test.CmpErr(t, errors.Errorf("property %q is read-only", testProp.Name), testProp.Set("whoops"))
}

func TestDaos_ContainerProperty_Layout(t *testing.T) {
	testReadOnlyContainerProperty(t, ContainerPropLayoutType)

	t.Run("valid layout", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropLayoutType)
		testProp.SetValue(uint64(ContainerLayoutPOSIX))
		test.AssertEqual(t, testProp.StringValue(), fmt.Sprintf("%s (%d)", ContainerLayoutPOSIX, ContainerLayoutPOSIX), "unexpected string value")
	})
	t.Run("unknown layout", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropLayoutType)
		testProp.SetValue(uint64(ContainerLayoutUnknown))
		test.AssertEqual(t, testProp.StringValue(), "unknown (0)", "unexpected string value")
	})
}

func TestDaos_ContainerProperty_ACL(t *testing.T) {
	testReadOnlyContainerProperty(t, ContainerPropACL)

	testACEStrs := stringSlice("A::OWNER@:rwdtTaAo", "A:G:GROUP@:rwtT")

	t.Run("ACL", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropACL)
		defer fillTestACLProp(t, testProp, testACEStrs...)()
		test.AssertEqual(t, testProp.StringValue(), strings.Join(testACEStrs, ", "), "unexpected string value")
	})

	t.Run("ACL to JSON", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropACL)
		defer fillTestACLProp(t, testProp, testACEStrs...)()

		gotJSON, err := testProp.MarshalJSON()
		test.CmpErr(t, nil, err)

		expValues := make([]string, len(testACEStrs))
		for i, aceStr := range testACEStrs {
			expValues[i] = `"` + aceStr + `"`
		}

		wantJSON := fmt.Sprintf(`{"value":[%s],"name":"%s","description":"%s"}`,
			strings.Join(expValues, ","), testProp.Name, testProp.Description)
		if diff := cmp.Diff(wantJSON, string(gotJSON)); diff != "" {
			t.Fatalf("unexpected JSON output (-want,+got):\n%s", diff)
		}
	})
}

func TestDaos_ContainerProperty_RootObjects(t *testing.T) {
	testReadOnlyContainerProperty(t, ContainerPropRootObjects)

	t.Run("root OIDs", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropRootObjects)
		rootOids := []ObjectID{
			{1, 1},
			{1, 2},
			{1, 3},
			{1, 4},
		}
		fillTestRootObjectsProp(t, testProp, rootOids...)
		expRootStrs := make([]string, len(rootOids))
		for i, oid := range rootOids {
			expRootStrs[i] = oid.String()
		}
		test.AssertEqual(t, testProp.StringValue(), strings.Join(expRootStrs, ", "), "unexpected string value")
	})

	t.Run("root OIDs to JSON", func(t *testing.T) {
		testProp := newTestContainerProperty(ContainerPropRootObjects)
		rootOids := []ObjectID{
			{1, 1},
			{1, 2},
		}
		fillTestRootObjectsProp(t, testProp, rootOids...)
		expRootStrs := make([]string, len(rootOids))
		for i, oid := range rootOids {
			expRootStrs[i] = `"` + oid.String() + `"`
		}

		gotJSON, err := testProp.MarshalJSON()
		test.CmpErr(t, nil, err)

		wantJSON := fmt.Sprintf(`{"value":[%s],"name":"%s","description":"%s"}`,
			strings.Join(expRootStrs, ","), testProp.Name, testProp.Description)
		if diff := cmp.Diff(wantJSON, string(gotJSON)); diff != "" {
			t.Fatalf("unexpected JSON output (-want,+got):\n%s", diff)
		}
	})
}

func newTestContainerProperty(propType ContainerPropType) *ContainerProperty {
	hdlr := propHdlrs[propType.String()]
	return &ContainerProperty{
		property:    *(*property)(newTestProperty()),
		hdlr:        hdlr,
		Type:        propType,
		Name:        propType.String(),
		Description: hdlr.shortDesc,
	}
}

func TestDaos_ContainerPropType_ToFromString(t *testing.T) {
	// This test is mostly just to ensure that every property type has
	// a stringer entry and a handler entry.
	for i := containerPropMin + 1; i < containerPropMax; i++ {
		t.Run(fmt.Sprintf("prop # %d", i), func(t *testing.T) {
			pt := ContainerPropType(0)
			err := pt.FromString(ContainerPropType(i).String())
			test.CmpErr(t, nil, err)
		})
	}

	// Add cases for an invalid property type and name.
	pt := ContainerPropType(0)
	test.AssertEqual(t, "unknown container property type 0", pt.String(), "unexpected String() output")
	test.CmpErr(t, errors.New("whoops"), pt.FromString("whoops"))
}

func TestDaos_ContainerProperty_Handlers(t *testing.T) {
	for i := containerPropMin + 1; i < containerPropMax; i++ {
		testProp := newTestContainerProperty(ContainerPropType(i))
		t.Run(testProp.Name+" check for nil handlers", func(t *testing.T) {
			// We don't care about the details in this test -- just verifying
			// that there are no nil input or output handlers. Will segfault
			// if a property has a nil handler.
			testProp.StringValue()
			if testProp.IsReadOnly() {
				return
			}
			testProp.Set("")
		})
	}
}

func testContainerPropertyInput(t *testing.T, propType ContainerPropType, input string, expErr error, expStrings ...string) {
	t.Helper()

	testProp := newTestContainerProperty(propType)
	err := testProp.Set(input)
	test.CmpErr(t, expErr, err)
	if expErr != nil {
		return
	}

	expString := input
	if len(expStrings) == 1 {
		expString = expStrings[0]
	}

	test.AssertEqual(t, testProp.StringValue(), expString, testProp.Name+": invalid string value")
}

func testContainerPropertyInputs(t *testing.T, propType ContainerPropType, validInputs []string) {
	t.Helper()

	badInputs := []string{"", "whoops"}
	for _, input := range badInputs {
		t.Run(input, func(t *testing.T) {
			expErr := errors.Errorf("invalid %s", propType.String())
			if len(validInputs) > 0 {
				expErr = errors.Errorf("invalid choice %q for %s", input, propType.String())
			}
			testContainerPropertyInput(t, propType, input, expErr)
		})
	}

	for _, input := range validInputs {
		t.Run(input, func(t *testing.T) {
			testContainerPropertyInput(t, propType, input, nil)
		})
	}
}

func stringSlice(inputs ...string) []string {
	return inputs
}

func TestDaos_boolStringer(t *testing.T) {
	for name, tc := range map[string]struct {
		input  uint64
		unset  bool
		expStr string
	}{
		"true": {
			input:  1,
			expStr: "true",
		},
		"false": {
			input:  0,
			expStr: "false",
		},
		"neither": {
			input: 42,
		},
		"unset": {
			unset:  true,
			expStr: "not set",
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestContainerProperty(ContainerPropScubberDisabled)
			if tc.unset {
				testProp.entry.dpe_flags = 1
			}
			testProp.SetValue(tc.input)
			if tc.expStr == "" {
				tc.expStr = propInvalidValue(testProp)
			}
			test.AssertEqual(t, tc.expStr, boolStringer(testProp), "unexpected string value")
		})
	}
}

func TestDaos_uintStringer(t *testing.T) {
	for name, tc := range map[string]struct {
		input  uint64
		unset  bool
		expStr string
	}{
		"42": {
			input:  42,
			expStr: "42",
		},
		"unset": {
			unset:  true,
			expStr: "not set",
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestContainerProperty(ContainerPropLayoutVersion)
			if tc.unset {
				testProp.entry.dpe_flags = 1
			}
			testProp.SetValue(tc.input)
			test.AssertEqual(t, tc.expStr, uintStringer(testProp), "unexpected string value")
		})
	}
}

func TestDaos_humanSizeStringer(t *testing.T) {
	for name, tc := range map[string]struct {
		input  uint64
		unset  bool
		expStr string
	}{
		"8192": {
			input:  8192,
			expStr: "8.0 KiB",
		},
		"unset": {
			unset:  true,
			expStr: "not set",
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestContainerProperty(ContainerPropLayoutVersion)
			if tc.unset {
				testProp.entry.dpe_flags = 1
			}
			testProp.SetValue(tc.input)
			test.AssertEqual(t, tc.expStr, humanSizeStringer(testProp), "unexpected string value")
		})
	}
}

func TestDaos_ContainerPropertyList_Allocate(t *testing.T) {
	for name, tc := range map[string]struct {
		count       int
		expCount    int
		expCapacity int
		expErr      error
	}{
		"negative count": {
			count:  -1,
			expErr: errors.New("negative count"),
		},
		"zero count": {
			count:       0,
			expCapacity: len(propHdlrs),
		},
		"positive count": {
			count:       5,
			expCapacity: 5,
		},
	} {
		t.Run(name, func(t *testing.T) {
			cpl, err := AllocateContainerPropertyList(tc.count)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}
			defer cpl.Free()

			test.AssertEqual(t, tc.expCount, cpl.Count(), "unexpected count")
			test.AssertEqual(t, tc.expCapacity, cap(cpl.entries), "unexpected capacity")
		})
	}
}

func TestDaos_ContainerPropertyList_New(t *testing.T) {
	for name, tc := range map[string]struct {
		propNames []string
		expCount  int
		expErr    error
	}{
		"empty list": {
			expCount: len(propHdlrs),
		},
		"valid list": {
			propNames: []string{
				ContainerPropLabel.String(),
				ContainerPropChecksumEnabled.String(),
			},
			expCount: 2,
		},
		"invalid property": {
			propNames: []string{"whoops"},
			expErr:    errors.New("unknown property"),
		},
		"duplicate property": {
			propNames: []string{
				ContainerPropLabel.String(),
				ContainerPropLabel.String(),
			},
			expErr: errors.New("duplicate property"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			cpl, err := NewContainerPropertyList(tc.propNames...)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}
			defer cpl.Free()

			test.AssertEqual(t, tc.expCount, cpl.Count(), "unexpected count")
		})
	}
}

func TestDaos_ContainerPropertyList_Copy(t *testing.T) {
	allocList := func(t *testing.T, count int) *ContainerPropertyList {
		t.Helper()

		cpl, err := AllocateContainerPropertyList(count)
		if err != nil {
			t.Fatal(err)
		}
		return cpl
	}

	for name, tc := range map[string]struct {
		sourceProps []string
		destList    *ContainerPropertyList
		expErr      error
	}{
		"nil source": {
			expErr: errors.New("nil source property list"),
		},
		"nil destination": {
			sourceProps: []string{ContainerPropLabel.String()},
			expErr:      errors.New("nil destination property list"),
		},
		"not enough room": {
			sourceProps: []string{ContainerPropLabel.String(), ContainerPropChecksumEnabled.String()},
			destList:    allocList(t, 1),
			expErr:      errors.New("destination property list does not have enough room"),
		},
		"dest is immutable": {
			sourceProps: []string{ContainerPropLabel.String()},
			destList: func() *ContainerPropertyList {
				cpl := allocList(t, 1)
				cpl.ToPtr()
				return cpl
			}(),
			expErr: ErrPropertyListImmutable,
		},
		"success": {
			sourceProps: []string{ContainerPropLabel.String(), ContainerPropChecksumEnabled.String()},
			destList:    allocList(t, 2),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var source *ContainerPropertyList
			if tc.sourceProps != nil {
				var err error
				source, err = NewContainerPropertyList(tc.sourceProps...)
				if err != nil {
					t.Fatal(err)
				}
				defer source.Free()
			}

			if tc.destList != nil {
				defer tc.destList.Free()
			}

			err := CopyContainerPropertyList(source, tc.destList)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			test.AssertEqual(t, source.Count(), tc.destList.Count(), "unexpected count")
		})
	}
}

func TestDaos_ContainerPropertyList_CopyFrom(t *testing.T) {
	t.Run("success", func(t *testing.T) {
		source, err := NewContainerPropertyList(ContainerPropLabel.String(), ContainerPropChecksumEnabled.String())
		if err != nil {
			t.Fatal(err)
		}
		defer source.Free()

		dest, err := AllocateContainerPropertyList(2)
		if err != nil {
			t.Fatal(err)
		}
		defer dest.Free()

		err = dest.CopyFrom(source)
		test.CmpErr(t, nil, err)
		test.AssertEqual(t, source.Count(), dest.Count(), "unexpected count")
	})
}

func TestDaos_ContainerPropertyList_DelEntryByType(t *testing.T) {
	for name, tc := range map[string]struct {
		initialProps  []ContainerPropType
		delProp       ContainerPropType
		expProps      []ContainerPropType
		makeImmutable bool
		expErr        error
	}{
		"delete from empty list": {
			delProp: ContainerPropLabel,
			expErr:  errors.New("cannot delete entry from list of size 0"),
		},
		"delete from single-entry list": {
			initialProps: []ContainerPropType{ContainerPropLabel},
			delProp:      ContainerPropLabel,
			expErr:       errors.New("cannot delete entry from list of size 1"),
		},
		"delete non-existent": {
			initialProps: []ContainerPropType{ContainerPropLabel, ContainerPropChecksumEnabled},
			delProp:      ContainerPropChecksumSize,
			expErr:       errors.Errorf("property %q not found in list", ContainerPropChecksumSize),
		},
		"delete from immutable list": {
			makeImmutable: true,
			initialProps:  []ContainerPropType{ContainerPropLabel, ContainerPropChecksumEnabled},
			delProp:       ContainerPropChecksumEnabled,
			expErr:        ErrPropertyListImmutable,
		},
		"delete existing": {
			initialProps: []ContainerPropType{ContainerPropLabel, ContainerPropChecksumEnabled},
			delProp:      ContainerPropChecksumEnabled,
			expProps:     []ContainerPropType{ContainerPropLabel},
		},
	} {
		t.Run(name, func(t *testing.T) {
			var cpl *ContainerPropertyList
			if len(tc.initialProps) == 0 {
				cpl = new(ContainerPropertyList)
			} else {
				var err error
				cpl, err = AllocateContainerPropertyList(len(tc.initialProps))
				if err != nil {
					t.Fatal(err)
				}
			}
			defer cpl.Free()

			for _, propType := range tc.initialProps {
				if _, err := cpl.AddEntryByType(propType); err != nil {
					t.Fatal(err)
				}
			}

			if tc.makeImmutable {
				cpl.ToPtr()
			}

			err := cpl.DelEntryByType(tc.delProp)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(len(tc.expProps), cpl.Count()); diff != "" {
				t.Fatalf("unexpected property count (-want, +got):\n%s", diff)
			}

			for i, expProp := range tc.expProps {
				if diff := cmp.Diff(expProp, cpl.Properties()[i].Type); diff != "" {
					t.Fatalf("unexpected property type (-want, +got):\n%s", diff)
				}
			}
		})
	}
}

func TestDaos_ContainerPropertyList_AddEntryByType(t *testing.T) {
	for name, tc := range map[string]struct {
		initialCount  int
		initialProps  []ContainerPropType
		addType       ContainerPropType
		expCount      int
		makeImmutable bool
		expErr        error
	}{
		"add duplicate": {
			initialCount: 2,
			initialProps: []ContainerPropType{ContainerPropLabel},
			addType:      ContainerPropLabel,
			expErr:       errors.New("duplicate property"),
		},
		"add invalid type": {
			addType: ContainerPropType(9999),
			expErr:  errors.New("invalid container property type"),
		},
		"add to full list": {
			initialProps: []ContainerPropType{ContainerPropLabel},
			addType:      ContainerPropChecksumEnabled,
			expErr:       errors.New("property list is full"),
		},
		"add to immutable list": {
			makeImmutable: true,
			addType:       ContainerPropLabel,
			expErr:        ErrPropertyListImmutable,
		},
		"add to empty list": {
			addType:  ContainerPropLabel,
			expCount: 1,
		},
		"add to non-empty list": {
			initialCount: 2,
			initialProps: []ContainerPropType{ContainerPropLabel},
			addType:      ContainerPropChecksumEnabled,
			expCount:     2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			allocCount := len(tc.initialProps)
			if tc.initialCount > 0 {
				allocCount = tc.initialCount
			}
			cpl, err := AllocateContainerPropertyList(allocCount)
			if err != nil {
				t.Fatal(err)
			}
			defer cpl.Free()

			for _, propType := range tc.initialProps {
				if _, err := cpl.AddEntryByType(propType); err != nil {
					t.Fatal(err)
				}
			}

			if tc.makeImmutable {
				cpl.ToPtr()
			}

			_, err = cpl.AddEntryByType(tc.addType)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			test.AssertEqual(t, tc.expCount, cpl.Count(), "unexpected count")
		})
	}
}

func TestDaos_ContainerPropertyList_AddEntryByName(t *testing.T) {
	for name, tc := range map[string]struct {
		initialCount  int
		initialProps  []ContainerPropType
		addName       string
		expCount      int
		makeImmutable bool
		expErr        error
	}{
		"add duplicate": {
			initialCount: 2,
			initialProps: []ContainerPropType{ContainerPropLabel},
			addName:      ContainerPropLabel.String(),
			expErr:       errors.New("duplicate property"),
		},
		"add invalid name": {
			addName: "whoops",
			expErr:  errors.New("unknown property"),
		},
		"add to full list": {
			initialProps: []ContainerPropType{ContainerPropLabel},
			addName:      ContainerPropChecksumEnabled.String(),
			expErr:       errors.New("property list is full"),
		},
		"add empty name": {
			addName: "",
			expErr:  errors.New("name must not be empty"),
		},
		"add name too long": {
			addName: strings.Repeat("x", maxNameLen+1),
			expErr:  errors.New("name too long"),
		},
		"add to immutable list": {
			makeImmutable: true,
			addName:       ContainerPropLabel.String(),
			expErr:        ErrPropertyListImmutable,
		},
		"add to empty list": {
			addName:  ContainerPropLabel.String(),
			expCount: 1,
		},
		"add to non-empty list": {
			initialCount: 2,
			initialProps: []ContainerPropType{ContainerPropLabel},
			addName:      ContainerPropChecksumEnabled.String(),
			expCount:     2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			allocCount := len(tc.initialProps)
			if tc.initialCount > 0 {
				allocCount = tc.initialCount
			}
			cpl, err := AllocateContainerPropertyList(allocCount)
			if err != nil {
				t.Fatal(err)
			}
			defer cpl.Free()

			for _, propType := range tc.initialProps {
				if _, err := cpl.AddEntryByName(propType.String()); err != nil {
					t.Fatal(err)
				}
			}

			if tc.makeImmutable {
				cpl.ToPtr()
			}

			_, err = cpl.AddEntryByName(tc.addName)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			test.AssertEqual(t, tc.expCount, cpl.Count(), "unexpected count")
		})
	}
}

func TestDaos_ContainerPropertyList_PropertyNames(t *testing.T) {
	for name, tc := range map[string]struct {
		propNames     []string
		excReadOnly   bool
		expPropNames  []string
		expDeprecated []string
	}{
		"default list": {
			expPropNames:  propHdlrs.keys(),
			expDeprecated: []string{"rf", "rf_lvl"},
		},
		"supplied list": {
			propNames: []string{
				ContainerPropLabel.String(),
				ContainerPropChecksumEnabled.String(),
				ContainerPropLayoutType.String(),
				ContainerPropRedunFactor.String(),
			},
			expPropNames: []string{
				ContainerPropChecksumEnabled.String(),
				ContainerPropLabel.String(),
				ContainerPropLayoutType.String(),
				ContainerPropRedunFactor.String(),
			},
			expDeprecated: []string{"rf"},
		},
		"exclude read-only": {
			propNames: []string{
				ContainerPropLabel.String(),
				ContainerPropChecksumEnabled.String(),
				ContainerPropLayoutType.String(),
				ContainerPropRedunFactor.String(),
			},
			excReadOnly: true,
			expPropNames: []string{
				ContainerPropChecksumEnabled.String(),
				ContainerPropLabel.String(),
				ContainerPropRedunFactor.String(),
			},
			expDeprecated: []string{"rf"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			cpl, err := NewContainerPropertyList(tc.propNames...)
			if err != nil {
				t.Fatal(err)
			}
			defer cpl.Free()

			gotPropNames := cpl.PropertyNames(tc.excReadOnly)
			expPropNames := append(tc.expPropNames, tc.expDeprecated...)
			sort.Strings(expPropNames)
			if diff := cmp.Diff(expPropNames, gotPropNames); diff != "" {
				t.Fatalf("unexpected property names (-want, +got):\n%s", diff)
			}
		})
	}
}

func TestDaos_ContainerPropertyList_Properties(t *testing.T) {
	for name, tc := range map[string]struct {
		propNames []string
		expCount  int
	}{
		"default list": {
			expCount: len(propHdlrs),
		},
		"valid list": {
			propNames: []string{
				ContainerPropLabel.String(),
				ContainerPropChecksumEnabled.String(),
			},
			expCount: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			cpl, err := NewContainerPropertyList(tc.propNames...)
			if err != nil {
				t.Fatal(err)
			}
			defer cpl.Free()

			props := cpl.Properties()
			test.AssertEqual(t, tc.expCount, len(props), "unexpected count")
		})
	}
}

func TestDaos_ContainerPropertyList_MustAddEntryByType(t *testing.T) {
	cpl, err := AllocateContainerPropertyList(1)
	if err != nil {
		t.Fatal(err)
	}
	defer cpl.Free()

	defer func() {
		if r := recover(); r == nil {
			t.Fatal("expected panic")
		}
	}()

	cpl.MustAddEntryByType(ContainerPropLabel)
	cpl.MustAddEntryByType(ContainerPropLabel)
}

func TestDaos_ContainerPropertyList_MustAddEntryByName(t *testing.T) {
	cpl, err := AllocateContainerPropertyList(1)
	if err != nil {
		t.Fatal(err)
	}
	defer cpl.Free()

	defer func() {
		if r := recover(); r == nil {
			t.Fatal("expected panic")
		}
	}()

	cpl.MustAddEntryByName(ContainerPropLabel.String())
	cpl.MustAddEntryByName(ContainerPropLabel.String())
}

func TestDaos_ContainerProperty(t *testing.T) {
	testValHdlr := func(cp *ContainerProperty, s string) error { return nil }
	testHdlr := &propHdlr{
		dpeType:   42,
		shortDesc: "test",
		nameHdlr: func(ph *propHdlr, cp *ContainerProperty, s string) error {
			return nil
		},
		valHdlrs: map[string]valHdlr{
			"a": testValHdlr,
			"b": testValHdlr,
		},
		deprecatedNames: []string{"old-test"},
		toString:        strValStringer,
	}
	testProp := &ContainerProperty{
		property:    *(*property)(newTestProperty()),
		hdlr:        testHdlr,
		Type:        ContainerPropType(42),
		Name:        "test",
		Description: testHdlr.shortDesc,
	}

	if diff := cmp.Diff([]string{"a", "b"}, testProp.SettableValues()); diff != "" {
		t.Fatalf("unexpected settable values (-want, +got):\n%s", diff)
	}
	if diff := cmp.Diff("test: not set", testProp.String()); diff != "" {
		t.Fatalf("unexpected string value (-want, +got):\n%s", diff)
	}
}
