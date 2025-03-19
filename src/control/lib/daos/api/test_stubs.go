//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package api

import (
	"sync"
)

var (
	testStubsMutex sync.Mutex
)

// LockTestStubs takes a lock on the package stubs to avoid interference
// between tests in different packages.
func LockTestStubs() {
	testStubsMutex.Lock()
}

// UnlockTestStubs releases the lock on the package stubs.
func UnlockTestStubs() {
	testStubsMutex.Unlock()
}

// ResetTestStubs will call the reset functions for all test stubs in order
// to reset state between tests.
func ResetTestStubs() {
	reset_daos_pool_stubs()
	reset_daos_cont_stubs()
	reset_dfs_stubs()
}
