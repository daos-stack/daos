//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import "testing"

func TestCopyStringToCharArray(t *testing.T) {
	for name, tc := range map[string]struct {
		input   string
		bufSize int
		want    string
	}{
		"simple string":      {"hello", 10, "hello"},
		"exact fit":          {"hello", 6, "hello"},
		"truncated":          {"hello world", 6, "hello"},
		"empty string":       {"", 10, ""},
		"single char buffer": {"hello", 1, ""},
	} {
		t.Run(name, func(t *testing.T) {
			if got := testCopyStringToCharArray(tc.input, tc.bufSize); got != tc.want {
				t.Fatalf("got %q, want %q", got, tc.want)
			}
		})
	}
}

func TestCopyStringToCharArrayNilDest(t *testing.T) {
	testCopyStringToNil()
}
