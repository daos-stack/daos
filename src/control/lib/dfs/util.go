package dfs

import (
	"context"
	"unsafe"

	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
)

/*
#include <stdlib.h>

#include <daos.h>
#include <daos_fs.h>
*/
import "C"

func freeString(s *C.char) {
	C.free(unsafe.Pointer(s))
}

func Init(parent context.Context) (context.Context, error) {
	if err := dfsError(C.dfs_init()); err != nil {
		return nil, err
	}
	return daosAPI.Init(parent)
}

func Fini(_ context.Context) error {
	return dfsError(C.dfs_fini())
}
