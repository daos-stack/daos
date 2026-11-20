//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

// cgo drivers for system_test.go (see test_helpers.go for why these can't
// live in the test file).

package main

/*
#include <stdint.h>
#include <daos_types.h>
*/
import "C"
import "runtime/cgo"

func callSystemStopRank(handle cgo.Handle, rank uint32, force bool) int {
	var fi C.int
	if force {
		fi = 1
	}
	return int(daos_control_system_stop_rank(C.uintptr_t(handle), C.d_rank_t(rank), fi))
}

func callSystemStop(handle cgo.Handle, force bool) int {
	var fi C.int
	if force {
		fi = 1
	}
	return int(daos_control_system_stop(C.uintptr_t(handle), fi))
}

func callSystemStartRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_start_rank(C.uintptr_t(handle), C.d_rank_t(rank)))
}

func callSystemStart(handle cgo.Handle) int {
	return int(daos_control_system_start(C.uintptr_t(handle)))
}

func callSystemReintRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_reint_rank(C.uintptr_t(handle), C.d_rank_t(rank)))
}

func callSystemQueryRankState(handle cgo.Handle, rank uint32, bufSize int) (string, int) {
	var out *C.char
	if bufSize > 0 {
		buf := make([]C.char, bufSize)
		out = &buf[0]
	}
	rc := int(daos_control_system_query_rank_state(C.uintptr_t(handle), C.d_rank_t(rank), out, C.size_t(bufSize)))
	return goCString(out), rc
}

func callSystemExcludeRank(handle cgo.Handle, rank uint32) int {
	return int(daos_control_system_exclude_rank(C.uintptr_t(handle), C.d_rank_t(rank)))
}
