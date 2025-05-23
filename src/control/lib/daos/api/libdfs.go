//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !test_stubs
// +build !test_stubs

package api

/*
#include <daos.h>
#include <daos_fs.h>

#cgo LDFLAGS: -ldfs
*/
import "C"

func dfs_mount(poolHdl C.daos_handle_t, contHdl C.daos_handle_t, flags C.int, dfs **C.dfs_t) C.int {
	return C.dfs_mount(poolHdl, contHdl, flags, dfs)
}

func dfs_umount(dfs *C.dfs_t) C.int {
	return C.dfs_umount(dfs)
}

func dfs_query(dfs *C.dfs_t, attrs *C.dfs_attr_t) C.int {
	return C.dfs_query(dfs, attrs)
}
