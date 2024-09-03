//
// (C) Copyright 2024 Intel Corporation.
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

#cgo LDFLAGS: -lcart -lgurt -ldaos -ldaos_common
*/
import "C"

func daos_init() C.int {
	return C.daos_init()
}

func daos_fini() {
	C.daos_fini()
}

func dc_agent_fini() {
	C.dc_agent_fini()
}

func daos_mgmt_get_sys_info(sys *C.char, sys_info **C.struct_daos_sys_info) C.int {
	return C.daos_mgmt_get_sys_info(sys, sys_info)
}

func daos_mgmt_put_sys_info(sys_info *C.struct_daos_sys_info) {
	C.daos_mgmt_put_sys_info(sys_info)
}
