//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"runtime/cgo"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

// TestBogusHandlePanicRecovery asserts that passing a non-zero, never-
// allocated handle to an exported function returns a DAOS error rather than
// propagating the cgo.Handle.Value() panic across the cgo boundary (where it
// would SIGABRT the process). Exercises the safeExport defer wiring.
func TestBogusHandlePanicRecovery(t *testing.T) {
	// Any non-zero integer that was never handed out by cgo.NewHandle will
	// cause cgo.Handle.Value() to panic.
	const bogus = cgo.Handle(0xdeadbeef)

	rc := callCheckSwitch(bogus, true)
	if rc == 0 {
		t.Fatalf("expected non-zero rc for bogus handle, got 0")
	}
	if rc != int(daos.MiscError) && rc != int(daos.InvalidInput) {
		t.Fatalf("expected MiscError or InvalidInput, got %d", rc)
	}
}
