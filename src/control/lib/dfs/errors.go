package dfs

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include "string.h"
*/
import "C"

type ErrnoErr struct {
	Errno  int
	Errstr string
}

func (e *ErrnoErr) Error() string {
	return fmt.Sprintf("errno %d (%s)", e.Errno, e.Errstr)
}

func dfsError(rc C.int) error {
	if rc == 0 {
		return nil
	}

	strErr := C.strerror(rc)
	return &ErrnoErr{int(rc), C.GoString(strErr)}
}

// daosError converts a return code from a DAOS API
// call to a Go error.
func daosError(rc C.int) error {
	if rc == 0 {
		return nil
	}
	return daos.Status(rc)
}
