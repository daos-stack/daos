//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package api

/*
#include <daos.h>
#include <daos_fs.h>
*/
import "C"
import (
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func reset_dfs_stubs() {
	reset_dfs_mount()
	reset_dfs_umount()
	reset_dfs_query()
}

var (
	dfs_mount_RC C.int = 0
)

func reset_dfs_mount() {
	dfs_mount_RC = 0
}

func dfs_mount(poolHdl C.daos_handle_t, contHdl C.daos_handle_t, flags C.int, dfs **C.dfs_t) C.int {
	return dfs_mount_RC
}

var (
	dfs_umount_RC C.int = 0
)

func reset_dfs_umount() {
	dfs_umount_RC = 0
}

func dfs_umount(dfs *C.dfs_t) C.int {
	return dfs_umount_RC
}

var (
	dfs_query_Attrs daos.POSIXAttributes = *daos_default_ContainerInfo.POSIXAttributes
	dfs_query_RC    C.int                = 0
)

func reset_dfs_query() {
	dfs_query_Attrs = *daos_default_ContainerInfo.POSIXAttributes
	dfs_query_RC = 0
}

func dfs_query(dfs *C.dfs_t, attrs *C.dfs_attr_t) C.int {
	if dfs_query_RC != 0 {
		return dfs_query_RC
	}

	attrs.da_chunk_size = C.uint64_t(dfs_query_Attrs.ChunkSize)
	attrs.da_dir_oclass_id = C.uint32_t(dfs_query_Attrs.DirObjectClass)
	attrs.da_file_oclass_id = C.uint32_t(dfs_query_Attrs.FileObjectClass)
	attrs.da_oclass_id = C.uint32_t(dfs_query_Attrs.ObjectClass)
	attrs.da_mode = C.uint32_t(dfs_query_Attrs.ConsistencyMode)
	C.strcpy(&attrs.da_hints[0], C.CString(dfs_query_Attrs.Hints))

	return dfs_query_RC
}
