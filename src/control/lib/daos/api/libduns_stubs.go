//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package api

import (
	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <daos.h>
#include <daos_uns.h>
*/
import "C"

var (
	duns_default_PoolID      string               = "test-pool"
	duns_default_ContainerID string               = "test-container"
	duns_default_Layout      daos.ContainerLayout = daos.ContainerLayoutPOSIX
)

func reset_duns_stubs() {
	reset_duns_resolve_path()
	reset_duns_link_cont()
	reset_duns_destroy_path()
}

var (
	duns_resolve_path_PoolID      string               = duns_default_PoolID
	duns_resolve_path_ContainerID string               = duns_default_ContainerID
	duns_resolve_path_Layout      daos.ContainerLayout = duns_default_Layout
	duns_resolve_path_RelPath     string
	duns_resolve_path_Count       int
	duns_resolve_path_RC          C.int = 0
)

func reset_duns_resolve_path() {
	duns_resolve_path_PoolID = duns_default_PoolID
	duns_resolve_path_ContainerID = duns_default_ContainerID
	duns_resolve_path_Layout = duns_default_Layout
	duns_resolve_path_RelPath = ""
	duns_resolve_path_Count = 0
	duns_resolve_path_RC = 0
}

func duns_resolve_path(path *C.char, attr *C.struct_duns_attr_t) C.int {
	duns_resolve_path_Count++
	if duns_resolve_path_RC != 0 {
		return duns_resolve_path_RC
	}

	// Copy pool ID to da_pool
	poolBytes := []byte(duns_resolve_path_PoolID)
	for i, b := range poolBytes {
		if i >= C.DAOS_PROP_LABEL_MAX_LEN {
			break
		}
		attr.da_pool[i] = C.char(b)
	}
	nullIdx := len(poolBytes)
	if nullIdx > C.DAOS_PROP_LABEL_MAX_LEN {
		nullIdx = C.DAOS_PROP_LABEL_MAX_LEN
	}
	attr.da_pool[nullIdx] = 0

	// Copy container ID to da_cont
	contBytes := []byte(duns_resolve_path_ContainerID)
	for i, b := range contBytes {
		if i >= C.DAOS_PROP_LABEL_MAX_LEN {
			break
		}
		attr.da_cont[i] = C.char(b)
	}
	nullIdx = len(contBytes)
	if nullIdx > C.DAOS_PROP_LABEL_MAX_LEN {
		nullIdx = C.DAOS_PROP_LABEL_MAX_LEN
	}
	attr.da_cont[nullIdx] = 0

	attr.da_type = C.daos_cont_layout_t(duns_resolve_path_Layout)

	if duns_resolve_path_RelPath != "" {
		attr.da_rel_path = C.CString(duns_resolve_path_RelPath)
	}

	return 0
}

var (
	duns_link_cont_SetContainerID string
	duns_link_cont_SetPath        string
	duns_link_cont_Count          int
	duns_link_cont_RC             C.int = 0
)

func reset_duns_link_cont() {
	duns_link_cont_SetContainerID = ""
	duns_link_cont_SetPath = ""
	duns_link_cont_Count = 0
	duns_link_cont_RC = 0
}

func duns_link_cont(poolHdl C.daos_handle_t, cont *C.char, path *C.char) C.int {
	duns_link_cont_Count++
	if duns_link_cont_RC != 0 {
		return duns_link_cont_RC
	}

	duns_link_cont_SetContainerID = C.GoString(cont)
	duns_link_cont_SetPath = C.GoString(path)

	return 0
}

var (
	duns_destroy_path_SetPath string
	duns_destroy_path_Count   int
	duns_destroy_path_RC      C.int = 0
)

func reset_duns_destroy_path() {
	duns_destroy_path_SetPath = ""
	duns_destroy_path_Count = 0
	duns_destroy_path_RC = 0
}

func duns_destroy_path(poolHdl C.daos_handle_t, path *C.char) C.int {
	duns_destroy_path_Count++
	if duns_destroy_path_RC != 0 {
		return duns_destroy_path_RC
	}

	duns_destroy_path_SetPath = C.GoString(path)

	return 0
}

func duns_destroy_attr(attr *C.struct_duns_attr_t) {
	// In the stub, we need to free any allocated strings
	if attr.da_rel_path != nil {
		freeString(attr.da_rel_path)
		attr.da_rel_path = nil
	}
	if attr.da_sys != nil {
		freeString(attr.da_sys)
		attr.da_sys = nil
	}
}
