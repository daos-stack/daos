//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import "unsafe"

/*
#include <stdlib.h>
*/
import "C"

// freeString frees a C string allocated with C.CString.
func freeString(s *C.char) {
	C.free(unsafe.Pointer(s))
}

// toCString converts a Go string to a C string which
// is returned with a closure to free the C memory.
func toCString(in string) (*C.char, func()) {
	cString := C.CString(in)
	return cString, func() { freeString(cString) }
}
