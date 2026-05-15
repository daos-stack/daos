//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestPropsFromC(t *testing.T) {
	t.Run("nil and empty", func(t *testing.T) {
		if got, err := testPropsFromC(nil, true); err != nil || got != nil {
			t.Fatalf("nil: got (%v, %v), want (nil, nil)", got, err)
		}
		if got, err := testPropsFromC(nil, false); err != nil || got != nil {
			t.Fatalf("empty: got (%v, %v), want (nil, nil)", got, err)
		}
	})

	t.Run("string, numeric, and enum-numeric mix", func(t *testing.T) {
		got, err := testPropsFromC([]testPropEntry{
			testPropStr(testPropPoLabel, "mypool"),
			testPropNum(testPropPoRedunFac, 2),
			testPropNum(testPropPoScrubMode, 2),
		}, false)
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		if len(got) != 3 {
			t.Fatalf("got %d props, want 3", len(got))
		}

		if got[0].Name != "label" || got[0].Value.String() != "mypool" {
			t.Errorf("props[0]=%s, want label:mypool", got[0])
		}
		if got[1].Name != "rd_fac" {
			t.Errorf("props[1].Name=%q, want rd_fac", got[1].Name)
		}
		if n, err := got[1].Value.GetNumber(); err != nil || n != 2 {
			t.Errorf("rd_fac=(%d, %v), want 2", n, err)
		}
		if got[2].Name != "scrub" {
			t.Errorf("props[2].Name=%q, want scrub", got[2].Name)
		}
		if n, err := got[2].Value.GetNumber(); err != nil || n != 2 {
			t.Errorf("scrub=(%d, %v), want 2", n, err)
		}
	})

	t.Run("owner and owner_group rejected", func(t *testing.T) {
		for _, pt := range []uint32{testPropPoOwner, testPropPoOwnerGroup} {
			_, err := testPropsFromC([]testPropEntry{testPropStr(pt, "alice@")}, false)
			if err != daos.NotSupported {
				t.Errorf("propType=%d: err=%v, want daos.NotSupported", pt, err)
			}
		}
	})

	t.Run("val-pointer props rejected", func(t *testing.T) {
		for _, pt := range []uint32{testPropPoACL, testPropPoSvcList} {
			_, err := testPropsFromC([]testPropEntry{testPropNum(pt, 0)}, false)
			if err != daos.NotSupported {
				t.Errorf("propType=%d: err=%v, want daos.NotSupported", pt, err)
			}
		}
	})

	t.Run("unknown property number rejected", func(t *testing.T) {
		_, err := testPropsFromC([]testPropEntry{testPropNum(0xbad, 0)}, false)
		if err != daos.NotSupported {
			t.Errorf("err=%v, want daos.NotSupported", err)
		}
	})
}
