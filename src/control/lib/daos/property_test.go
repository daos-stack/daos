//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"testing"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func newTestProperty() *property {
	return &property{
		entry: &_Ctype_struct_daos_prop_entry{}, // managed by Go; no need to free()
	}
}

func TestDaos_Property_IsUnset(t *testing.T) {
	for name, tc := range map[string]struct {
		setup func(*property)
		unset bool
	}{
		"unset": {
			setup: func(p *property) {
				p.entry.dpe_flags = 1
			},
			unset: true,
		},
		"set": {
			setup: func(p *property) {
				p.entry.dpe_flags = 0
			},
			unset: false,
		},
		"nil entry": {
			setup: func(p *property) {
				p.entry = nil
			},
			unset: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestProperty()
			tc.setup(testProp)
			test.AssertEqual(t, tc.unset, testProp.IsUnset(), "IsUnset mismatch")
		})
	}
}

func TestDaos_Property_SetGetString(t *testing.T) {
	for name, tc := range map[string]struct {
		setup    func(*property)
		expected string
	}{
		"empty string": {
			setup: func(p *property) {
				p.SetString("")
			},
			expected: "",
		},
		"non-empty string": {
			setup: func(p *property) {
				p.SetString("test string")
			},
			expected: "test string",
		},
		"nil entry": {
			setup: func(p *property) {
				p.entry = nil
			},
			expected: "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestProperty()
			tc.setup(testProp)
			test.AssertEqual(t, tc.expected, testProp.GetString(), "GetString mismatch")
		})
	}
}

func TestDaos_Property_SetGetValue(t *testing.T) {
	for name, tc := range map[string]struct {
		setup    func(*property)
		expected uint64
	}{
		"zero value": {
			expected: 0,
		},
		"non-zero value": {
			setup: func(p *property) {
				p.SetValue(12345)
			},
			expected: 12345,
		},
		"nil entry": {
			setup: func(p *property) {
				p.entry = nil
			},
			expected: ^uint64(0),
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestProperty()
			if tc.setup != nil {
				tc.setup(testProp)
			}
			test.AssertEqual(t, tc.expected, testProp.GetValue(), "GetValue mismatch")
		})
	}
}

func TestDaos_Property_SetGetValuePtr(t *testing.T) {
	for name, tc := range map[string]struct {
		setup    func(*property)
		expected unsafe.Pointer
	}{
		"nil pointer": {
			setup: func(p *property) {
				p.SetValuePtr(nil)
			},
			expected: nil,
		},
		"non-nil pointer": {
			setup: func(p *property) {
				p.SetValuePtr(unsafe.Pointer(uintptr(12345)))
			},
			expected: unsafe.Pointer(uintptr(12345)),
		},
		"nil entry": {
			setup: func(p *property) {
				p.entry = nil
			},
			expected: nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestProperty()
			tc.setup(testProp)
			test.AssertEqual(t, tc.expected, testProp.GetValuePtr(), "GetValuePtr mismatch")
		})
	}
}

func TestDaos_NewPropertyList(t *testing.T) {
	for name, tc := range map[string]struct {
		count       int
		expectError error
	}{
		"negative count": {
			count:       -1,
			expectError: errors.New("negative count"),
		},
		"zero count": {
			count:       0,
			expectError: nil,
		},
		"positive count": {
			count:       5,
			expectError: nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			pl, err := newPropertyList(tc.count)
			test.CmpErr(t, tc.expectError, err)
			if err != nil {
				return
			}
			defer pl.Free()
			test.AssertEqual(t, 0, pl.Count(), "initial count should be zero")
			test.AssertEqual(t, tc.count, cap(pl.entries), "capacity mismatch")
		})
	}
}

func TestDaos_PropertyList_Count(t *testing.T) {
	pl, err := newPropertyList(5)
	if err != nil {
		t.Fatalf("newPropertyList failed: %s", err)
	}
	defer pl.Free()
	test.AssertEqual(t, 0, pl.Count(), "initial count should be zero")
	pl.cProps.dpp_nr = 3
	test.AssertEqual(t, 3, pl.Count(), "count should be 3")
}

func TestDaos_PropertyList_ToPtr(t *testing.T) {
	pl, err := newPropertyList(5)
	if err != nil {
		t.Fatalf("newPropertyList failed: %s", err)
	}
	defer pl.Free()
	ptr := pl.ToPtr()
	test.AssertEqual(t, unsafe.Pointer(pl.cProps), ptr, "ToPtr mismatch")
	test.AssertTrue(t, pl.immutable, "property list should be immutable")
}
