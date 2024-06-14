//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"os"
	"strings"

	"github.com/pkg/errors"
)

/*
#cgo LDFLAGS: -lgurt

#include <daos/debug.h>
*/
import "C"

const (
	// DefaultDebugMask defines the basic debug mask.
	DefaultDebugMask = "DEBUG,MEM=ERR,OBJECT=ERR,PLACEMENT=ERR"
	// DefaultInfoMask defines the basic info mask.
	DefaultInfoMask = "INFO"
	// DefaultErrorMask defines the basic error mask.
	DefaultErrorMask = "ERROR"
)

// InitLogging initializes the DAOS logging system.
func InitLogging(masks ...string) (func(), error) {
	mask := strings.Join(masks, ",")
	if mask == "" {
		mask = DefaultInfoMask
	}
	os.Setenv("D_LOG_MASK", mask)

	if rc := C.daos_debug_init(nil); rc != 0 {
		return func() {}, errors.Wrap(Status(rc), "daos_debug_init() failed")
	}

	return func() {
		C.daos_debug_fini()
	}, nil
}
