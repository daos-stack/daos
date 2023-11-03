package client

import (
	"context"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <string.h>
*/
import "C"

var (
	errInvalidContainerHandle = errors.New("container handle was nil or invalid")
	errInvalidPoolHandle      = errors.New("pool handle was nil or invalid")
)

// dfsError converts a return code from a DFS API
// call to a Go error.
func dfsError(rc C.int) error {
	if rc == 0 {
		return nil
	}

	strErr := C.strerror(rc)
	return errors.Errorf("DFS error %d: %s", rc, C.GoString(strErr))
}

// daosError converts a return code from a DAOS API
// call to a Go error.
func daosError(rc C.int) error {
	if rc == 0 {
		return nil
	}
	return daos.Status(rc)
}

func ctxErr(err error) error {
	switch {
	case err == nil:
		return nil
	case errors.Is(err, context.Canceled):
		return errors.Wrap(daos.Canceled, "DAOS API context canceled")
	case errors.Is(err, context.DeadlineExceeded):
		return errors.Wrap(daos.TimedOut, "DAOS API context deadline exceeded")
	default:
		return errors.Wrap(daos.MiscError, "DAOS API context error")
	}
}
