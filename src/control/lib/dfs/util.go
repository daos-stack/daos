package dfs

import (
	"unsafe"
)

/*
#include <stdlib.h>
*/
import "C"

func freeString(s *C.char) {
	C.free(unsafe.Pointer(s))
}
