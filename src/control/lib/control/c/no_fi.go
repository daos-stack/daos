//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build !fault_injection

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <uuid/uuid.h>
*/
import "C"

import (
	"github.com/daos-stack/daos/src/control/lib/daos"
)

//export daos_control_fault_inject
func daos_control_fault_inject(
	handle C.uintptr_t,
	poolUUID *C.uuid_t,
	mgmtSvc C.int,
	fault *C.char,
) C.int {
	return C.int(daos.NotSupported)
}
