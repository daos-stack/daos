//
// (C) Copyright 2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

/*
#include <stdlib.h>
#include <uuid/uuid.h>

#include <daos_prop.h>

#include "util.h"
*/
import "C"

func goBool2int(in bool) (out C.int) {
	if in {
		out = 1
	}
	return
}

func copyUUID(dst *C.uuid_t, src uuid.UUID) error {
	if dst == nil {
		return errors.Wrap(daos.InvalidInput, "nil dest uuid_t")
	}

	for i, v := range src {
		dst[i] = C.uchar(v)
	}

	return nil
}

func uuidToC(in uuid.UUID) (out C.uuid_t) {
	for i, v := range in {
		out[i] = C.uchar(v)
	}

	return
}

func uuidFromC(cUUID C.uuid_t) (uuid.UUID, error) {
	return uuid.FromBytes(C.GoBytes(unsafe.Pointer(&cUUID[0]), C.int(len(cUUID))))
}

func freeString(s *C.char) {
	C.free(unsafe.Pointer(s))
}

func iterStringsBuf(cBuf unsafe.Pointer, expected C.size_t, cb func(string)) error {
	var curLen C.size_t

	// Create a Go slice for easy iteration (no pointer arithmetic in Go).
	bufSlice := unsafe.Slice((*C.char)(cBuf), expected)
	for total := C.size_t(0); total < expected; total += curLen + 1 {
		chunk := bufSlice[total:]
		curLen = C.strnlen(&chunk[0], expected-total)

		if curLen >= expected-total {
			return errors.Wrap(daos.NoMemory, "corrupt buffer")
		}

		chunk = bufSlice[total : total+curLen]
		cb(C.GoString(&chunk[0]))
	}

	return nil
}

func rankSetFromC(cRankList *C.d_rank_list_t) (*ranklist.RankSet, error) {
	if cRankList == nil {
		return nil, errors.Wrap(daos.InvalidInput, "nil ranklist")
	}

	cRankSlice := unsafe.Slice(cRankList.rl_ranks, cRankList.rl_nr)
	rs := ranklist.NewRankSet()
	for _, cRank := range cRankSlice {
		rs.Add(ranklist.Rank(cRank))
	}

	return rs, nil
}

func ranklistFromGo(rs *ranklist.RankSet) *C.d_rank_list_t {
	if rs == nil {
		return nil
	}

	rl := C.d_rank_list_alloc(C.uint32_t(rs.Count()))
	cRanks := unsafe.Slice(rl.rl_ranks, rs.Count())
	for i, r := range rs.Ranks() {
		cRanks[i] = C.d_rank_t(r)
	}

	return rl
}
