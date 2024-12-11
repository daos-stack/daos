//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"errors"
	"testing"
	"unsafe"

	"github.com/golang/mock/gomock"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

/*
#include <gurt/common.h>
#include <daos_pool.h>
*/
import "C"

type MockDAOSInterface interface {
	daos_pool_query(poolHdl C.daos_handle_t, rlPtr *C.d_rank_list_t, poolInfo C.daos_pool_info_t, arg3 unsafe.Pointer, arg4 unsafe.Pointer) C.int
	d_rank_list_free(rl *C.d_rank_list_t)
}

type MockDAOS struct {
	ctrl *gomock.Controller
}

func (m MockDAOS) daos_pool_query(poolHdl C.daos_handle_t, rlPtr *C.d_rank_list_t, poolInfo *C.daos_pool_info_t, arg3 unsafe.Pointer, arg4 unsafe.Pointer) C.int {
	return C.int(0)
}

func (m MockDAOS) d_rank_list_free(rl C.d_rank_list_t) {}

func TestQueryPoolRankLists(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	mockDAOS := &MockDAOS{ctrl: ctrl}

	mockDAOS.daos_pool_query = func(poolHdl C.daos_handle_t, rlPtr *C.d_rank_list_t, poolInfo C.daos_pool_info_t, arg3 unsafe.Pointer, arg4 unsafe.Pointer) C.int {
		*poolInfo = C.daos_pool_info_t{
			pi_bits: C.uint64_t(PoolQueryOptionEnabledEngines),
		}
		rlPtr = (C.d_rank_list_t)(unsafe.Pointer(&C.d_rank_list_t{
			rl_nr:    C.uint32_t(2),
			rl_ranks: (*C.uint32_t)(unsafe.Pointer(&[2]C.uint32_t{1, 2}[0])),
		}))
		return C.int(0)
	}

	origDaosPoolQuery := daos_pool_query
	origDaosRankListFree := d_rank_list_free
	defer func() {
		daos_pool_query = origDaosPoolQuery
		d_rank_list_free = origDaosRankListFree
	}()

	daos_pool_query = mockDAOS.daos_pool_query
	d_rank_list_free = mockDAOS.d_rank_list_free

	queryMask := PoolQueryMask(PoolQueryOptionEnabledEngines)
	poolHdl := C.daos_handle_t{}
	poolInfo, err := queryPoolRankLists(poolHdl, queryMask)

	assert.NoError(t, err)
	assert.NotNil(t, poolInfo)
	assert.Equal(t, 2, len(poolInfo.EnabledRanks.Ranks))
	assert.Equal(t, RankSet{Ranks: []uint32{1, 2}}, poolInfo.EnabledRanks)
}
