//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build fault_injection && !release

package main

/*
#include <stdlib.h>
#include <stdint.h>
#include <uuid/uuid.h>
*/
import "C"
import (
	"runtime/cgo"
	"unsafe"
)

func callFaultInject(handle cgo.Handle, fault string, mgmtSvc bool) int {
	cFault := C.CString(fault)
	defer C.free(unsafe.Pointer(cFault))
	var poolUUID C.uuid_t
	var ms C.int
	if mgmtSvc {
		ms = 1
	}
	return int(daos_control_fault_inject(C.uintptr_t(handle), &poolUUID, ms, cFault))
}
