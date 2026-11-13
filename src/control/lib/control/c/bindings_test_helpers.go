//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

// cgo drivers for bindings_test.go (see test_helpers.go for why these can't
// live in the test file).

package main

/*
#include <stdint.h>

#include <daos/control_types.h>
*/
import "C"
import "runtime/cgo"

func callInit(configFile, logFile, logLevel string) (cgo.Handle, int) {
	cfg, cfgFree := cString(configFile)
	defer cfgFree()
	lf, lfFree := cString(logFile)
	defer lfFree()
	ll, llFree := cString(logLevel)
	defer llFree()

	args := C.struct_daos_control_init_args{
		dcia_config_file: cfg,
		dcia_log_file:    lf,
		dcia_log_level:   ll,
	}
	var handle C.uintptr_t
	rc := int(daos_control_init(&args, &handle))
	return cgo.Handle(handle), rc
}

func callInitNilHandleOut() int {
	var args C.struct_daos_control_init_args
	return int(daos_control_init(&args, nil))
}

func callFini(handle cgo.Handle) { daos_control_fini(C.uintptr_t(handle)) }
