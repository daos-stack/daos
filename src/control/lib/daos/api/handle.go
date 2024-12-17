//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"context"
	"fmt"
	"unsafe"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/google/uuid"
	"github.com/pkg/errors"
)

/*
#include <daos.h>

#cgo LDFLAGS: -ldaos_common
*/
import "C"

type (
	// ctxHdlKey is a type used for storing handles as context values.
	ctxHdlKey string

	// connHandle is an opaque type used to represent a DAOS connection (pool or container).
	connHandle struct {
		UUID       uuid.UUID
		Label      string
		daosHandle C.daos_handle_t
	}
)

// invalidate clears the handle so that it cannot be reused inadvertently.
func (ch *connHandle) invalidate() {
	if ch == nil {
		return
	}
	ch.UUID = uuid.Nil
	ch.Label = ""
	ch.daosHandle.cookie = 0
}

// FillHandle copies the handle to the supplied memory buffer.
// NB: Caller is responsible for keeping the copy in sync with
// this handle -- use of this method should be discouraged.
func (ch *connHandle) FillHandle(cHandle unsafe.Pointer) error {
	if ch == nil || cHandle == nil {
		return errors.New("invalid handle")
	}
	(*C.daos_handle_t)(cHandle).cookie = ch.daosHandle.cookie

	return nil
}

// IsValid returns true if the pool or container handle is valid.
func (ch *connHandle) IsValid() bool {
	if ch == nil {
		return false
	}
	return bool(daos_handle_is_valid(ch.daosHandle))
}

func (ch *connHandle) String() string {
	if ch == nil {
		return "<nil>"
	}
	id := ch.Label
	if id == "" {
		id = logging.ShortUUID(ch.UUID)
	}
	return fmt.Sprintf("%s:%t", id, ch.IsValid())
}

// fromCtx retrieves the handle from the supplied context, if available.
func (ch *connHandle) fromCtx(ctx context.Context, key ctxHdlKey) error {
	if ch == nil {
		return errors.New("nil connHandle")
	}
	if ctx == nil {
		return errNilCtx
	}
	hdl, ok := ctx.Value(key).(connHandle)
	if !ok {
		return errNoCtxHdl
	}

	*ch = hdl
	return nil
}

// toCtx returns a new context with the handle stashed in it.
func (ch *connHandle) toCtx(ctx context.Context, key ctxHdlKey) context.Context {
	if ch == nil {
		return ctx
	}
	return context.WithValue(ctx, key, *ch)
}
