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

func TestControlC_CopyStringToCharArray(t *testing.T) {
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

func TestControlC_CopyStringToCharArrayNilDest(t *testing.T) {
	testCopyStringToNil()
}

func TestControlC_PropsFromC(t *testing.T) {
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

	t.Run("ACL, owner, owner_group, svc_list skipped by propsFromC", func(t *testing.T) {
		// These entry types are handled by poolCreateExtrasFromC, not the
		// registry. propsFromC must skip them silently (not return an error).
		// ACL and svc_list use val-pointer storage; owner/owner_group use str.
		for _, e := range []testPropEntry{
			testPropNum(testPropPoACL, 0),
			testPropStr(testPropPoOwner, "alice@"),
			testPropStr(testPropPoOwnerGroup, "admins@"),
			testPropNum(testPropPoSvcList, 0),
		} {
			got, err := testPropsFromC([]testPropEntry{e}, false)
			if err != nil {
				t.Errorf("propType=%d: unexpected error %v", e.Type, err)
			}
			if len(got) != 0 {
				t.Errorf("propType=%d: got %d props, want 0 (skipped)", e.Type, len(got))
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
