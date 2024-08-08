//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package daos

import "C"

var (
	daos_debug_init_RC C.int = 0
)

func daos_debug_init(log_file *C.char) C.int {
	return daos_debug_init_RC
}

func daos_debug_fini() {}
