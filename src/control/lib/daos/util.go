package daos

import (
	"unsafe"
)

/*
#include <stdlib.h>
#include <uuid/uuid.h>

#include <daos_prop.h>
*/
import "C"

// daosError converts a return code from a DAOS API
// call to a Go error.
func daosError(rc C.int) error {
	if rc == 0 {
		return nil
	}
	return Status(rc)
}

func freeString(s *C.char) {
	C.free(unsafe.Pointer(s))
}
