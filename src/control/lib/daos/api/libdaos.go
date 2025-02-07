//
// (C) Copyright 2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !test_stubs
// +build !test_stubs

package api

/*
#include <daos.h>
#include <daos_mgmt.h>
#include <daos/agent.h>
#include <daos/pool.h>

#cgo LDFLAGS: -lcart -lgurt -ldaos -ldaos_common
*/
import "C"
import "unsafe"

func daos_init() C.int {
	return C.daos_init()
}

func daos_fini() {
	C.daos_fini()
}

func dc_agent_fini() {
	C.dc_agent_fini()
}

func daos_handle_is_valid(handle C.daos_handle_t) C.bool {
	return C.daos_handle_is_valid(handle)
}

func daos_mgmt_get_sys_info(sys *C.char, sys_info **C.struct_daos_sys_info) C.int {
	return C.daos_mgmt_get_sys_info(sys, sys_info)
}

func daos_mgmt_put_sys_info(sys_info *C.struct_daos_sys_info) {
	C.daos_mgmt_put_sys_info(sys_info)
}

func daos_pool_connect(poolID *C.char, sys *C.char, flags C.uint32_t, poolHdl *C.daos_handle_t, poolInfo *C.daos_pool_info_t, ev *C.struct_daos_event) C.int {
	return C.daos_pool_connect(poolID, sys, flags, poolHdl, poolInfo, ev)
}

func daos_pool_disconnect(poolHdl C.daos_handle_t) C.int {
	// Hack for NLT fault injection testing: If the rc
	// is -DER_NOMEM, retry once in order to actually
	// shut down and release resources.
	rc := C.daos_pool_disconnect(poolHdl, nil)
	if rc == -C.DER_NOMEM {
		rc = C.daos_pool_disconnect(poolHdl, nil)
		// DAOS-8866, daos_pool_disconnect() might have failed, but worked anyway.
		if rc == -C.DER_NO_HDL {
			rc = -C.DER_SUCCESS
		}
	}

	return rc
}

func daos_pool_query(poolHdl C.daos_handle_t, rankList **C.d_rank_list_t, poolInfo *C.daos_pool_info_t, props *C.daos_prop_t, ev *C.struct_daos_event) C.int {
	return C.daos_pool_query(poolHdl, rankList, poolInfo, props, ev)
}

func daos_pool_query_target(poolHdl C.daos_handle_t, tgt C.uint32_t, rank C.uint32_t, info *C.daos_target_info_t, ev *C.struct_daos_event) C.int {
	return C.daos_pool_query_target(poolHdl, tgt, rank, info, ev)
}

func daos_pool_list_attr(poolHdl C.daos_handle_t, buf *C.char, size *C.size_t, ev *C.struct_daos_event) C.int {
	return C.daos_pool_list_attr(poolHdl, buf, size, ev)
}

func daos_pool_get_attr(poolHdl C.daos_handle_t, n C.int, names **C.char, values *unsafe.Pointer, sizes *C.size_t, ev *C.struct_daos_event) C.int {
	return C.daos_pool_get_attr(poolHdl, n, names, values, sizes, ev)
}

func daos_pool_set_attr(poolHdl C.daos_handle_t, n C.int, names **C.char, values *unsafe.Pointer, sizes *C.size_t, ev *C.struct_daos_event) C.int {
	return C.daos_pool_set_attr(poolHdl, n, names, values, sizes, ev)
}

func daos_pool_del_attr(poolHdl C.daos_handle_t, n C.int, name **C.char, ev *C.struct_daos_event) C.int {
	return C.daos_pool_del_attr(poolHdl, n, name, ev)
}

func daos_mgmt_list_pools(sysName *C.char, poolCount *C.daos_size_t, pools *C.daos_mgmt_pool_info_t, ev *C.struct_daos_event) C.int {
	return C.daos_mgmt_list_pools(sysName, poolCount, pools, ev)
}
