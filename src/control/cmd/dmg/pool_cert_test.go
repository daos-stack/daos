//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"
	"time"
)

func TestDmg_validityFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		exp    time.Duration
		expErr bool
	}{
		"days":      {in: "90d", exp: 90 * 24 * time.Hour},
		"weeks":     {in: "26w", exp: 26 * 7 * 24 * time.Hour},
		"years":     {in: "2y", exp: 2 * 365 * 24 * time.Hour},
		"no unit":   {in: "90", expErr: true},
		"bad unit":  {in: "90h", expErr: true},
		"zero":      {in: "0d", expErr: true},
		"negative":  {in: "-1d", expErr: true},
		"empty":     {in: "", expErr: true},
		"not a num": {in: "xd", expErr: true},
	} {
		t.Run(name, func(t *testing.T) {
			var f validityFlag
			err := f.UnmarshalFlag(tc.in)
			if tc.expErr {
				if err == nil {
					t.Fatalf("expected error for %q", tc.in)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if f.Duration != tc.exp {
				t.Fatalf("got %s, want %s", f.Duration, tc.exp)
			}
		})
	}
}
