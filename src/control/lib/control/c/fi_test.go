//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build fault_injection

package main

import (
	"testing"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestParseFaultClass(t *testing.T) {
	// Every well-formed class name must round-trip back through
	// parseFaultClass so the fault-injection export can't silently
	// swallow a typo. We don't pin the full class list here (it lives in
	// control.SystemCheckFindingClass and evolves with the check engine);
	// we pin the parse contract — non-empty string produces either a
	// class or an error, empty/garbage produces an error.
	for name, tc := range map[string]struct {
		input     string
		expectErr bool
		expectNot chkpb.CheckInconsistClass
	}{
		"empty rejected": {
			input:     "",
			expectErr: true,
		},
		"garbage rejected": {
			input:     "not_a_real_class",
			expectErr: true,
		},
		"canonical name accepted": {
			// CIC_POOL_LESS_SVC_WITHOUT_QUORUM is the first non-NONE
			// class declared in chk.proto; SystemCheckFindingClass
			// stringifies by stripping the CIC_ prefix, and FromString
			// normalizes by re-adding it, so the canonical form is
			// the uppercase underscore-form without the prefix.
			input:     "POOL_LESS_SVC_WITHOUT_QUORUM",
			expectNot: chkpb.CheckInconsistClass_CIC_NONE,
		},
	} {
		t.Run(name, func(t *testing.T) {
			got, err := parseFaultClass(tc.input)
			if tc.expectErr {
				if err == nil {
					t.Fatalf("expected error, got class=%v", got)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if got == tc.expectNot {
				t.Fatalf("class=%v, want something other than %v", got, tc.expectNot)
			}
		})
	}
}

// TestFaultInjectInvalidHandle pins the panic-recovery contract for the
// fi-specific export — a bogus handle must surface as an error rc, not a
// panic escaping the cgo boundary.
func TestFaultInjectInvalidHandle(t *testing.T) {
	rc := callFaultInject(0, "pool-less-svc-without-quorum", false)
	if rc == 0 {
		t.Fatalf("expected non-zero rc for invalid handle")
	}
	// The specific status depends on how the handle lookup reports
	// failure; accept anything that isn't Success.
	if rc == int(daos.Success) {
		t.Fatalf("expected error rc, got Success")
	}
}
