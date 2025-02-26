//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"fmt"
	"unsafe"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

/*
#include <daos.h>

#cgo LDFLAGS: -ldaos_common
*/
import "C"

const (
	// MissingPoolLabel defines a default label set when a pool connection was made by UUID.
	MissingPoolLabel = "<no pool label supplied>"
	// MissingContainerLabel defines a default label set when a container connection was made by UUID.
	MissingContainerLabel = "<no container label supplied>"
)

type (
	// ctxHdlKey is a type used for storing handles as context values.
	ctxHdlKey string

	// connHandle is an opaque type used to represent a DAOS connection (pool or container).
	connHandle struct {
		UUID       uuid.UUID
		Label      string
		daosHandle C.daos_handle_t
		fromCtx    bool
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

// FillHandle copies the handle to the supplied pointer,
// which must be a reference to a C.daos_handle_t.
// NB: Caller is responsible for keeping the copy in sync with
// this handle -- use of this method should be discouraged as
// it is provided for compatibility with older code that calls
// into libdaos directly.
func (ch *connHandle) FillHandle(cHandle unsafe.Pointer) error {
	if !ch.IsValid() {
		return errors.New("invalid handle")
	}
	if cHandle == nil {
		return errors.New("nil DAOS handle pointer")
	}
	(*C.daos_handle_t)(cHandle).cookie = ch.daosHandle.cookie

	return nil
}

// IsValid returns true if the pool or container handle is valid.
func (ch *connHandle) IsValid() bool {
	if ch == nil {
		return false
	}
	if (ch.Label == "" || ch.Label == MissingPoolLabel || ch.Label == MissingContainerLabel) && ch.UUID == uuid.Nil {
		return false
	}

	return bool(daos_handle_is_valid(ch.daosHandle))
}

// ID returns the label if available, otherwise the UUID.
func (ch *connHandle) ID() string {
	if ch == nil {
		return "<nil>"
	}
	if !ch.IsValid() {
		return "<invalid>"
	}

	id := ch.Label
	if id == "" || id == MissingPoolLabel || id == MissingContainerLabel {
		id = ch.UUID.String()
	}

	return id
}

func (ch *connHandle) String() string {
	if ch == nil {
		return "<nil>:false"
	}
	if !ch.IsValid() {
		return "<invalid>:false"
	}

	id := ch.Label
	if id == "" || id == MissingPoolLabel || id == MissingContainerLabel {
		id = logging.ShortUUID(ch.UUID)
	}
	return fmt.Sprintf("%s:%t", id, ch.IsValid())
}
