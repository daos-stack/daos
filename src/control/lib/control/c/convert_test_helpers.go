//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

// cgo drivers for convert_test.go (see test_helpers.go for why these can't
// live in the test file).

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <daos_types.h>
#include <daos_prop.h>
#include <gurt/common.h>
*/
import "C"
import (
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// testPropsFromC drives propsFromC end-to-end: builds a C prop from entries,
// converts it back through the library, and frees the C allocation.
// Passing nilInput=true runs propsFromC(nil).
func testPropsFromC(entries []testPropEntry, nilInput bool) ([]*daos.PoolProperty, error) {
	var cProp *C.daos_prop_t
	if !nilInput {
		cProp = buildCProp(entries)
		defer C.daos_prop_free(cProp)
	}
	return propsFromC(cProp)
}

// testCopyStringToCharArray exercises copyStringToCharArray with a Go input
// and the given buffer size, returning the resulting Go string.
func testCopyStringToCharArray(s string, bufSize int) string {
	if bufSize <= 0 {
		copyStringToCharArray(s, nil, bufSize)
		return ""
	}
	buf := make([]C.char, bufSize)
	copyStringToCharArray(s, &buf[0], bufSize)
	return goCString(&buf[0])
}

// testCopyStringToNil exercises the nil-dest fast path.
func testCopyStringToNil() {
	copyStringToCharArray("hello", nil, 10)
}

// testRankListRoundTrip builds a C rank list from the Go input, converts it
// back via rankListFromC, and returns the resulting slice.
func testRankListRoundTrip(ranks []uint32) []uint32 {
	crl := C.d_rank_list_alloc(C.uint32_t(len(ranks)))
	defer C.d_rank_list_free(crl)
	if len(ranks) > 0 {
		cRanks := unsafe.Slice(crl.rl_ranks, len(ranks))
		for i, r := range ranks {
			cRanks[i] = C.d_rank_t(r)
		}
	}
	got := rankListFromC(crl)
	out := make([]uint32, len(got))
	for i, r := range got {
		out[i] = uint32(r)
	}
	return out
}

// testCopyRankListTo exercises copyRankListToC with the given input and
// destination capacity; returns the ranks written and rl_nr after the call.
func testCopyRankListTo(input []uint32, outCap int) (got []uint32, rlNr uint32) {
	if outCap <= 0 {
		return nil, 0
	}
	dst := C.d_rank_list_alloc(C.uint32_t(outCap))
	defer C.d_rank_list_free(dst)

	goRanks := make([]ranklist.Rank, len(input))
	for i, r := range input {
		goRanks[i] = ranklist.Rank(r)
	}
	copyRankListToC(goRanks, dst, outCap)

	rlNr = uint32(dst.rl_nr)
	got = make([]uint32, rlNr)
	if rlNr > 0 {
		cRanks := unsafe.Slice(dst.rl_ranks, rlNr)
		for i, r := range cRanks {
			got[i] = uint32(r)
		}
	}
	return got, rlNr
}
