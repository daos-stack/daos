//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"
)

func TestCopyStringToCharArray(t *testing.T) {
	for name, tc := range map[string]struct {
		input    string
		bufSize  int
		expected string
	}{
		"simple string": {
			input:    "hello",
			bufSize:  10,
			expected: "hello",
		},
		"exact fit": {
			input:    "hello",
			bufSize:  6, // 5 chars + null
			expected: "hello",
		},
		"truncated": {
			input:    "hello world",
			bufSize:  6,
			expected: "hello", // truncated to fit with null terminator
		},
		"empty string": {
			input:    "",
			bufSize:  10,
			expected: "",
		},
		"single char buffer": {
			input:    "hello",
			bufSize:  1,
			expected: "", // only null terminator fits
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf := newTestCharArray(tc.bufSize)
			testCopyStringToCharArray(tc.input, buf, tc.bufSize)

			got := buf.toString()
			if got != tc.expected {
				t.Fatalf("expected %q, got %q", tc.expected, got)
			}
		})
	}
}

func TestCopyStringToCharArrayNilDest(t *testing.T) {
	// Should not panic with nil destination
	testCopyStringToCharArray("hello", nil, 10)
}

func TestParseUUID(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expectErr bool
		expected  [16]byte
	}{
		"valid UUID with dashes": {
			input:     "12345678-1234-1234-1234-123456789abc",
			expectErr: false,
			expected:  [16]byte{0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc},
		},
		"valid UUID without dashes": {
			input:     "123456781234123412341234567890ab",
			expectErr: false,
			expected:  [16]byte{0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab},
		},
		"all zeros": {
			input:     "00000000-0000-0000-0000-000000000000",
			expectErr: false,
			expected:  [16]byte{},
		},
		"all ones": {
			input:     "ffffffff-ffff-ffff-ffff-ffffffffffff",
			expectErr: false,
			expected:  [16]byte{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		},
		"too short": {
			input:     "12345678-1234-1234-1234",
			expectErr: true,
		},
		"too long": {
			input:     "12345678-1234-1234-1234-123456789abc-extra",
			expectErr: true,
		},
		"invalid hex": {
			input:     "12345678-1234-1234-1234-12345678xxxx",
			expectErr: true,
		},
		"empty string": {
			input:     "",
			expectErr: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			got, err := testParseUUID(tc.input)

			if tc.expectErr {
				if err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}

			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			if got != tc.expected {
				t.Fatalf("expected %v, got %v", tc.expected, got)
			}
		})
	}
}
