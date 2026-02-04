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

// TestPropsFromC verifies that propsFromC reads each pool-property storage
// class correctly: string props from dpe_str, numeric/enum props from dpe_val,
// and val-pointer props (ACL, SVC_LIST) rejected rather than silently coerced.
func TestPropsFromC(t *testing.T) {
	t.Run("nil and empty", func(t *testing.T) {
		if got, err := propsFromC(nil); err != nil || got != nil {
			t.Fatalf("nil: got (%v, %v), want (nil, nil)", got, err)
		}

		p := newTestPoolProp(0)
		defer p.free()
		if got, err := propsFromC(p.ptr()); err != nil || got != nil {
			t.Fatalf("empty: got (%v, %v), want (nil, nil)", got, err)
		}
	})

	t.Run("string, numeric, and enum-numeric mix", func(t *testing.T) {
		p := newTestPoolProp(3)
		defer p.free()
		p.setEntryStr(0, testPropPoLabel, "mypool")
		p.setEntryVal(1, testPropPoRedunFac, 2)
		p.setEntryVal(2, testPropPoScrubMode, 2 /* = timed */)

		got, err := propsFromC(p.ptr())
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		if len(got) != 3 {
			t.Fatalf("got %d props, want 3", len(got))
		}

		// label: string round-trip.
		if got[0].Name != "label" {
			t.Errorf("props[0].Name=%q, want %q", got[0].Name, "label")
		}
		if s := got[0].Value.String(); s != "mypool" {
			t.Errorf("label value=%q, want %q", s, "mypool")
		}

		// rd_fac: plain numeric.
		if got[1].Name != "rd_fac" {
			t.Errorf("props[1].Name=%q, want %q", got[1].Name, "rd_fac")
		}
		if n, err := got[1].Value.GetNumber(); err != nil || n != 2 {
			t.Errorf("rd_fac value=(%d, %v), want 2", n, err)
		}

		// scrub: enum-numeric — registry has named values but we set via
		// dpe_val, so the raw number is what we should see back.
		if got[2].Name != "scrub" {
			t.Errorf("props[2].Name=%q, want %q", got[2].Name, "scrub")
		}
		if n, err := got[2].Value.GetNumber(); err != nil || n != 2 {
			t.Errorf("scrub value=(%d, %v), want 2", n, err)
		}
	})

	t.Run("owner and owner_group rejected", func(t *testing.T) {
		// OWNER / OWNER_GROUP are conveyed through pool-create's uid/gid
		// parameters; passing them in the prop list must fail loudly so a
		// future registry addition can't silently read a dpe_str pointer as uint64.
		for _, propType := range []uint32{testPropPoOwner, testPropPoOwnerGroup} {
			p := newTestPoolProp(1)
			p.setEntryStr(0, propType, "alice@")
			got, err := propsFromC(p.ptr())
			p.free()
			if err != daos.NotSupported {
				t.Errorf("propType=%d: err=%v got=%v, want daos.NotSupported",
					propType, err, got)
			}
		}
	})

	t.Run("val-pointer props rejected", func(t *testing.T) {
		// ACL and SVC_LIST are not settable at pool-create time. Accepting
		// them would have read dpe_val (a pointer) as a uint64.
		for _, propType := range []uint32{testPropPoACL, testPropPoSvcList} {
			p := newTestPoolProp(1)
			p.setEntryVal(0, propType, 0)
			got, err := propsFromC(p.ptr())
			p.free()
			if err != daos.NotSupported {
				t.Errorf("propType=%d: err=%v got=%v, want daos.NotSupported",
					propType, err, got)
			}
		}
	})

	t.Run("unknown property number rejected", func(t *testing.T) {
		p := newTestPoolProp(1)
		defer p.free()
		p.setEntryVal(0, 0xbad, 0)
		if got, err := propsFromC(p.ptr()); err != daos.NotSupported {
			t.Errorf("got (%v, %v), want (nil, daos.NotSupported)", got, err)
		}
	})
}
