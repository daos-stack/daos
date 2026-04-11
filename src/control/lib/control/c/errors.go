//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"errors"
	"os"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

// errorToRC converts a Go error to a DAOS return code.
func errorToRC(err error) int {
	if err == nil {
		return 0
	}

	// Check for daos.Status errors first
	var ds daos.Status
	if errors.As(err, &ds) {
		return int(ds)
	}

	// Check for control plane specific errors
	if errors.Is(err, errInvalidHandle) {
		return int(daos.InvalidInput)
	}
	if errors.Is(err, control.ErrNoConfigFile) {
		return int(daos.BadPath)
	}

	// Check for common OS errors
	if errors.Is(err, os.ErrNotExist) {
		return int(daos.Nonexistent)
	}
	if errors.Is(err, os.ErrPermission) {
		return int(daos.NoPermission)
	}

	// Check for context errors
	if errors.Is(err, context.DeadlineExceeded) {
		return int(daos.TimedOut)
	}
	if errors.Is(err, context.Canceled) {
		return int(daos.Canceled)
	}

	// Default to DER_MISC for unknown errors
	return int(daos.MiscError)
}
