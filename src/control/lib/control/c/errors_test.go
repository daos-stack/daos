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
	"testing"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestErrorToRC(t *testing.T) {
	for name, tc := range map[string]struct {
		err   error
		expRC int
	}{
		"nil error": {
			err:   nil,
			expRC: 0,
		},
		"daos.Status - NoSpace": {
			err:   daos.NoSpace,
			expRC: int(daos.NoSpace),
		},
		"daos.Status - NoPermission": {
			err:   daos.NoPermission,
			expRC: int(daos.NoPermission),
		},
		"daos.Status - NotFound": {
			err:   daos.Nonexistent,
			expRC: int(daos.Nonexistent),
		},
		"daos.Status - TimedOut": {
			err:   daos.TimedOut,
			expRC: int(daos.TimedOut),
		},
		"wrapped daos.Status": {
			err:   errors.New("wrapper: " + daos.NoSpace.Error()),
			expRC: int(daos.MiscError), // wrapped string doesn't unwrap
		},
		"errInvalidHandle": {
			err:   errInvalidHandle,
			expRC: int(daos.InvalidInput),
		},
		"control.ErrNoConfigFile": {
			err:   control.ErrNoConfigFile,
			expRC: int(daos.BadPath),
		},
		"os.ErrNotExist": {
			err:   os.ErrNotExist,
			expRC: int(daos.Nonexistent),
		},
		"os.ErrPermission": {
			err:   os.ErrPermission,
			expRC: int(daos.NoPermission),
		},
		"context.DeadlineExceeded": {
			err:   context.DeadlineExceeded,
			expRC: int(daos.TimedOut),
		},
		"context.Canceled": {
			err:   context.Canceled,
			expRC: int(daos.Canceled),
		},
		"unknown error": {
			err:   errors.New("some unknown error"),
			expRC: int(daos.MiscError),
		},
	} {
		t.Run(name, func(t *testing.T) {
			got := testErrorToRC(tc.err)
			if got != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, got)
			}
		})
	}
}
