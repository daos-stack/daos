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

func freeString(s *C.char) {
	C.free(unsafe.Pointer(s))
}
