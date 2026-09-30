//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestParseKernelConfig(t *testing.T) {
	for name, tc := range map[string]struct {
		input  string
		expect KernelConfig
	}{
		"empty": {
			input:  "",
			expect: KernelConfig{},
		},
		"comments only": {
			input: `# This is a comment
# Another comment
`,
			expect: KernelConfig{},
		},
		"builtin option": {
			input:  "CONFIG_NUMA=y",
			expect: KernelConfig{"CONFIG_NUMA": "y"},
		},
		"module option": {
			input:  "CONFIG_EXT4_FS=m",
			expect: KernelConfig{"CONFIG_EXT4_FS": "m"},
		},
		"disabled option": {
			input:  "CONFIG_DEBUG=n",
			expect: KernelConfig{"CONFIG_DEBUG": "n"},
		},
		"string value": {
			input:  `CONFIG_DEFAULT_HOSTNAME="(none)"`,
			expect: KernelConfig{"CONFIG_DEFAULT_HOSTNAME": `"(none)"`},
		},
		"integer value": {
			input:  "CONFIG_LOG_BUF_SHIFT=17",
			expect: KernelConfig{"CONFIG_LOG_BUF_SHIFT": "17"},
		},
		"hex value": {
			input:  "CONFIG_PHYSICAL_START=0x1000000",
			expect: KernelConfig{"CONFIG_PHYSICAL_START": "0x1000000"},
		},
		"mixed content": {
			input: `#
# General setup
#
CONFIG_NUMA=y
CONFIG_SMP=y
CONFIG_MODULES=m
CONFIG_DEBUG_INFO=n
# CONFIG_EXPERT is not set
CONFIG_DEFAULT_HOSTNAME="(none)"
CONFIG_HZ=1000

# Memory management
CONFIG_TRANSPARENT_HUGEPAGE=y
`,
			expect: KernelConfig{
				"CONFIG_NUMA":                 "y",
				"CONFIG_SMP":                  "y",
				"CONFIG_MODULES":              "m",
				"CONFIG_DEBUG_INFO":           "n",
				"CONFIG_DEFAULT_HOSTNAME":     `"(none)"`,
				"CONFIG_HZ":                   "1000",
				"CONFIG_TRANSPARENT_HUGEPAGE": "y",
			},
		},
		"whitespace handling": {
			input: `  CONFIG_FOO=y
	CONFIG_BAR=m
`,
			expect: KernelConfig{
				"CONFIG_FOO": "y",
				"CONFIG_BAR": "m",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := parseKernelConfig(strings.NewReader(tc.input))
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			if diff := cmp.Diff(tc.expect, result); diff != "" {
				t.Errorf("unexpected result (-want +got):\n%s", diff)
			}
		})
	}
}

func TestKernelConfig_IsEnabled(t *testing.T) {
	config := KernelConfig{
		"CONFIG_NUMA":     "y",
		"CONFIG_EXT4_FS":  "m",
		"CONFIG_DEBUG":    "n",
		"CONFIG_HZ":       "1000",
		"CONFIG_HOSTNAME": `"localhost"`,
	}

	for name, tc := range map[string]struct {
		key    string
		expect bool
	}{
		"builtin enabled": {
			key:    "CONFIG_NUMA",
			expect: true,
		},
		"module enabled": {
			key:    "CONFIG_EXT4_FS",
			expect: true,
		},
		"explicitly disabled": {
			key:    "CONFIG_DEBUG",
			expect: false,
		},
		"integer value": {
			key:    "CONFIG_HZ",
			expect: false,
		},
		"string value": {
			key:    "CONFIG_HOSTNAME",
			expect: false,
		},
		"missing option": {
			key:    "CONFIG_NONEXISTENT",
			expect: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := config.IsEnabled(tc.key)
			if result != tc.expect {
				t.Errorf("expected %v, got %v", tc.expect, result)
			}
		})
	}

	// Verify nil config doesn't panic and returns false.
	for name, tc := range map[string]struct {
		key    string
		expect bool
	}{
		"nil config": {
			key:    "CONFIG_NUMA",
			expect: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var nilCfg KernelConfig
			result := nilCfg.IsEnabled(tc.key)
			if result != tc.expect {
				t.Errorf("expected %v, got %v", tc.expect, result)
			}
		})
	}
}

func TestKernelConfig_GetValue(t *testing.T) {
	config := KernelConfig{
		"CONFIG_NUMA": "y",
		"CONFIG_HZ":   "1000",
	}

	for name, tc := range map[string]struct {
		key       string
		expectVal string
		expectOK  bool
	}{
		"present option": {
			key:       "CONFIG_NUMA",
			expectVal: "y",
			expectOK:  true,
		},
		"integer option": {
			key:       "CONFIG_HZ",
			expectVal: "1000",
			expectOK:  true,
		},
		"missing option": {
			key:       "CONFIG_NONEXISTENT",
			expectVal: "",
			expectOK:  false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			val, ok := config.GetValue(tc.key)
			if val != tc.expectVal {
				t.Errorf("expected value %q, got %q", tc.expectVal, val)
			}
			if ok != tc.expectOK {
				t.Errorf("expected ok=%v, got ok=%v", tc.expectOK, ok)
			}
		})
	}
}
