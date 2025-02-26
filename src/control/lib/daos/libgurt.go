//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !test_stubs
// +build !test_stubs

package daos

/*
#cgo LDFLAGS: -lgurt

#include <daos/debug.h>
*/
import "C"

func daos_debug_init(log_file *C.char) C.int {
	return C.daos_debug_init(log_file)
}

func daos_debug_fini() {
	C.daos_debug_fini()
}
