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
		// IgnoreAnyFunction (rather than IgnoreTopFunction) is required here
		// because once a raft node is elected leader (which happens almost
		// immediately for a single-node mock DB), run() dispatches into
		// runFollower()/runCandidate()/runLeader(), and the leader path further
		// enters leaderLoop(). From then on the goroutine's top-of-stack frame
		// is leaderLoop, not run, so IgnoreTopFunction("...run") would no
		// longer match.
		goleak.IgnoreAnyFunction("github.com/hashicorp/raft.(*Raft).run"),
		goleak.IgnoreTopFunction("github.com/hashicorp/raft.(*Raft).runFSM"),
		goleak.IgnoreTopFunction("github.com/hashicorp/raft.(*Raft).runSnapshots"),
		// runLeader() unconditionally spawns a periodic metrics goroutine the
		// moment a node becomes leader. It is an independent goroutine (only
		// "created by" runLeader, not called from it), so it needs its own
		// entry.
		goleak.IgnoreTopFunction("github.com/hashicorp/raft.emitLogStoreMetrics"),
		goleak.IgnoreTopFunction("google.golang.org/grpc.(*Server).Serve"),
		// execRestart is stubbed out in unit tests that exercise this path, so
		// the goroutine is harmless but is expected to still be sleeping when
		// the test binary exits.
		goleak.IgnoreAnyFunction("github.com/daos-stack/daos/src/control/server.(*mgmtSvc).scheduleControlPlaneRestart.func1"),
	)
}
