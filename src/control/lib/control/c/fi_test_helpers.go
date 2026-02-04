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

// callFaultInject drives daos_control_fault_inject from Go with a handle,
// fault name, and mgmt-svc flag. Lives in a non-test file so cgo is permitted
// (Go disallows `import "C"` from _test.go in c-shared mode).
func callFaultInject(handle cgo.Handle, fault string, mgmtSvc bool) int {
	cFault := C.CString(fault)
	defer C.free(unsafe.Pointer(cFault))

	poolUUID := newTestUUID()
	var msInt C.int
	if mgmtSvc {
		msInt = 1
	}
	return int(daos_control_fault_inject(
		C.uintptr_t(handle),
		poolUUID.ptr(),
		msInt,
		cFault,
	))
}
