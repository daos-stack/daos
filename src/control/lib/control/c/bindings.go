//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package main provides C bindings for the DAOS control API.
// This library is built with -buildmode=c-shared to produce libdaos_control.so.
package main

/*
#include <stdlib.h>
#include <stdint.h>

// daos_control_init_args contains initialization options.
// All fields are optional; NULL/empty uses defaults.
struct daos_control_init_args {
	const char *config_file;  // Path to dmg config file (NULL for default/insecure)
	const char *log_file;     // Path to log file (NULL disables logging)
	const char *log_level;    // Log level: debug, info, notice, error (NULL for notice)
};
*/
import "C"
import (
	"runtime/cgo"
)

// main is required for c-shared build mode but is not called.
func main() {}

//export daos_control_init
func daos_control_init(args *C.struct_daos_control_init_args, handleOut *C.uintptr_t) C.int {
	if handleOut == nil {
		return C.int(errorToRC(errInvalidHandle))
	}

	var cfgPath, logFilePath, logLevelStr string
	if args != nil {
		cfgPath = goString(args.config_file)
		logFilePath = goString(args.log_file)
		logLevelStr = goString(args.log_level)
	}

	ctx, err := newContext(cfgPath, logFilePath, logLevelStr)
	if err != nil {
		return C.int(errorToRC(err))
	}

	h := cgo.NewHandle(ctx)
	*handleOut = C.uintptr_t(h)
	return 0
}

//export daos_control_fini
func daos_control_fini(handle C.uintptr_t) {
	if handle == 0 {
		return
	}

	h := cgo.Handle(handle)
	ctx, ok := h.Value().(*ctrlContext)
	if !ok {
		return
	}
	ctx.close()
	h.Delete()
}
