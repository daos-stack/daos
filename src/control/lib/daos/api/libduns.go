//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !test_stubs
// +build !test_stubs

package api

/*
#include <daos.h>
#include <daos_uns.h>

#cgo LDFLAGS: -lduns
*/
import "C"

func duns_resolve_path(path *C.char, attr *C.struct_duns_attr_t) C.int {
	return C.duns_resolve_path(path, attr)
}

func duns_link_cont(poolHdl C.daos_handle_t, cont *C.char, path *C.char) C.int {
	return C.duns_link_cont(poolHdl, cont, path)
}

func duns_destroy_path(poolHdl C.daos_handle_t, path *C.char) C.int {
	return C.duns_destroy_path(poolHdl, path)
}

func duns_destroy_attr(attr *C.struct_duns_attr_t) {
	C.duns_destroy_attr(attr)
}
