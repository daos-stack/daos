//
// (C) Copyright 2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

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
	ErrNoSystemRanks          = errors.New("no ranks in system")
	ErrContextHandleConflict  = errors.New("context already contains a handle for a different pool or container")
	ErrInvalidPoolHandle      = errors.New("pool handle is nil or invalid")
	ErrInvalidContainerHandle = errors.New("container handle is nil or invalid")

	errNilCtx   = errors.New("nil context")
	errNoCtxHdl = errors.New("no handle in context")
)

// dfsError converts a return code from a DFS API
// call to a Go error.
func dfsError(rc C.int) error {
	if rc == 0 {
		return nil
	}

	strErr := C.strerror(rc)
	return errors.Errorf("errno %d (%s)", rc, C.GoString(strErr))
}

// daosError converts a return code from a DAOS API
// call to a Go error.
func daosError(rc C.int) error {
	return daos.ErrorFromRC(int(rc))
}

// ctxErr recasts a context error as a DAOS error.
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
