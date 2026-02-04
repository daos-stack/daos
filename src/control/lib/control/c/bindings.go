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

#include <daos/control_types.h>
*/
import "C"
import (
	"fmt"
	"os"
	"runtime/cgo"
)

// main is required for c-shared build mode but is not called.
func main() {}

//export daos_control_init
func daos_control_init(args *C.struct_daos_control_init_args, handleOut *C.uintptr_t) (rc C.int) {
	defer recoverExport(&rc)

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
	defer recoverExportVoid()

	if handle == 0 {
		return
	}

	h := cgo.Handle(handle)
	ctx, ok := h.Value().(*ctrlContext)
	if !ok {
		fmt.Fprintf(os.Stderr,
			"daos_control_fini: handle %#x does not refer to a control context (leaked)\n",
			uintptr(handle))
		return
	}
	ctx.close()
	h.Delete()
}
