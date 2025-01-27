//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"os"
	"testing"
)

// TestMain defines package-level setup and teardown logic.
// NB: Any packages that run tests which depend on the API stubs
// should copy this function in order to avoid interference
// between parallel tests. Go parallelizes testing across
// packages (but not within a package) by default.
//
// Long-term, this should be phased out as API users should mock
// the API instead of relying on the stubs.
func TestMain(m *testing.M) {
	LockTestStubs()
	ResetTestStubs()
	defer UnlockTestStubs()
	os.Exit(m.Run())
	ResetTestStubs()
}
