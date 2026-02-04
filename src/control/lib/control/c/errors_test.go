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

func TestStatusFromMsg(t *testing.T) {
	for name, tc := range map[string]struct {
		msg    string
		expSt  daos.Status
		expHit bool
	}{
		"empty":                 {msg: "", expHit: false},
		"no parens":             {msg: "something bad happened", expHit: false},
		"plain daos.Status":     {msg: daos.NoSpace.Error(), expSt: daos.NoSpace, expHit: true},
		"wrapped prefix":        {msg: "wrapper: " + daos.Busy.Error(), expSt: daos.Busy, expHit: true},
		"wrapped prefix+suffix": {msg: "oops: " + daos.Nonexistent.Error() + ": tail", expSt: daos.Nonexistent, expHit: true},
		"skip unrelated parens": {
			// The first "(7)" is a rank number, not a status. Parser
			// must skip it and find the real code.
			msg:    "failed rank(7): " + daos.TimedOut.Error(),
			expSt:  daos.TimedOut,
			expHit: true,
		},
		"positive numbers rejected": {msg: "something (42)", expHit: false},
		"non-numeric rejected":      {msg: "something (oops)", expHit: false},
	} {
		t.Run(name, func(t *testing.T) {
			st, ok := statusFromMsg(tc.msg)
			if ok != tc.expHit {
				t.Fatalf("hit=%v, want %v", ok, tc.expHit)
			}
			if ok && st != tc.expSt {
				t.Fatalf("status=%d (%s), want %d (%s)", st, st, tc.expSt, tc.expSt)
			}
		})
	}
}

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
		"wrapped daos.Status (string-embedded)": {
			// Many control-plane paths stringify a Status into an
			// errors.New/Errorf message. The errorToRC fallback
			// recovers the embedded code so callers see the real
			// status instead of MiscError.
			err:   errors.New("wrapper: " + daos.NoSpace.Error()),
			expRC: int(daos.NoSpace),
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
			got := errorToRC(tc.err)
			if got != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, got)
			}
		})
	}
}
