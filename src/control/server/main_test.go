//
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"

	"go.uber.org/goleak"
)

// TestMain runs a goroutine-leak check across the package's test suite after
// all tests have completed. This is intended to catch tests that start
// background goroutines (e.g. via a manager's start() method) without
// stopping them, which can accumulate and eventually starve or hang the
// wider test binary.
func TestMain(m *testing.M) {
	goleak.VerifyTestMain(m,
		// Background goroutines owned by imported packages that are not
		// expected to be torn down synchronously within a single test.
		goleak.IgnoreTopFunction("github.com/hashicorp/raft.(*raft).run"),
		goleak.IgnoreTopFunction("github.com/hashicorp/raft.(*raft).runFSM"),
		goleak.IgnoreTopFunction("github.com/hashicorp/raft.(*raft).runSnapshots"),
		goleak.IgnoreTopFunction("google.golang.org/grpc.(*Server).Serve"),
		// scheduleControlPlaneRestart() intentionally spawns a fire-and-forget
		// goroutine that outlives the gRPC handler by design (it waits 500ms for
		// the response to be sent before restarting the control plane process).
		// execRestart is stubbed out in unit tests that exercise this path, so
		// the goroutine is harmless but is expected to still be sleeping when
		// the test binary exits.
		goleak.IgnoreAnyFunction("github.com/daos-stack/daos/src/control/server.(*mgmtSvc).scheduleControlPlaneRestart.func1"),
	)
}
