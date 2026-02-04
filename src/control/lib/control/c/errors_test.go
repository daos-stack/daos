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
	"strings"
	"testing"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

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
			msg:    "failed rank(7): " + daos.TimedOut.Error(),
			expSt:  daos.TimedOut,
			expHit: true,
		},
		"negative parens without DER_ prefix rejected": {
			msg:    "failed after retry(-1): something else",
			expHit: false,
		},
		"DER_-prefix anchored through wrapper": {
			msg:    "failed after retry(-1): " + daos.InvalidInput.Error(),
			expSt:  daos.InvalidInput,
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

func TestUidGidLookupErrorsAreInvalidInput(t *testing.T) {
	const nonexistent = uint32(0xFFFFFFFE)

	_, err := uidToUsername(nonexistent)
	if err == nil {
		t.Fatalf("uidToUsername(%d) unexpectedly succeeded — host has this UID", nonexistent)
	}
	if got := errorToRC(err); got != int(daos.InvalidInput) {
		t.Fatalf("uidToUsername rc=%d, want InvalidInput(%d)", got, int(daos.InvalidInput))
	}

	_, err = gidToGroupname(nonexistent)
	if err == nil {
		t.Fatalf("gidToGroupname(%d) unexpectedly succeeded — host has this GID", nonexistent)
	}
	if got := errorToRC(err); got != int(daos.InvalidInput) {
		t.Fatalf("gidToGroupname rc=%d, want InvalidInput(%d)", got, int(daos.InvalidInput))
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
		"wrapped daos.Status (string-embedded)": {
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
		"context.DeadlineExceeded": {
			err:   context.DeadlineExceeded,
			expRC: int(daos.TimedOut),
		},
		"unknown error": {
			err:   errors.New("some unknown error"),
			expRC: int(daos.MiscError),
		},
		"gRPC Unavailable": {
			err:   status.Error(codes.Unavailable, "transport unavailable"),
			expRC: int(daos.Unreachable),
		},
		"gRPC DeadlineExceeded": {
			err:   status.Error(codes.DeadlineExceeded, "deadline"),
			expRC: int(daos.TimedOut),
		},
		"gRPC PermissionDenied": {
			err:   status.Error(codes.PermissionDenied, "no"),
			expRC: int(daos.NoPermission),
		},
		"gRPC Unauthenticated": {
			err:   status.Error(codes.Unauthenticated, "no"),
			expRC: int(daos.NoPermission),
		},
		"gRPC NotFound": {
			err:   status.Error(codes.NotFound, "missing"),
			expRC: int(daos.Nonexistent),
		},
		"gRPC AlreadyExists": {
			err:   status.Error(codes.AlreadyExists, "exists"),
			expRC: int(daos.Exists),
		},
		"gRPC ResourceExhausted": {
			err:   status.Error(codes.ResourceExhausted, "full"),
			expRC: int(daos.NoSpace),
		},
		"gRPC InvalidArgument": {
			err:   status.Error(codes.InvalidArgument, "bad"),
			expRC: int(daos.InvalidInput),
		},
		"gRPC Unimplemented": {
			err:   status.Error(codes.Unimplemented, "nope"),
			expRC: int(daos.NotImpl),
		},
		"gRPC Aborted": {
			err:   status.Error(codes.Aborted, "again"),
			expRC: int(daos.TryAgain),
		},
		"gRPC Unknown falls through to MiscError": {
			err:   status.Error(codes.Unknown, "unclassified"),
			expRC: int(daos.MiscError),
		},
		"embedded DER_ wins over gRPC code": {
			// Server-side DAOS errors come back through gRPC; the explicit
			// DER_ token in the message must take precedence over the
			// transport-class fallback.
			err:   status.Error(codes.Unavailable, "wrapper: "+daos.Busy.Error()),
			expRC: int(daos.Busy),
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

func TestSummarizeRPCErr(t *testing.T) {
	// gRPC formats embedded HTTP responses with %q, so \r\n appears as the
	// literal 4-char sequence in the error message.
	zscaler := `rpc error: code = Unavailable desc = connection error: ` +
		`desc = "transport: error while dialing: failed to do connect handshake, ` +
		`response: \"HTTP/1.0 502 Bad Gateway\r\nConnection: close\r\n` +
		`Content-Type: text/html\r\nServer: Zscaler/6.2\r\n\r\n<html>...body...</html>\""`

	for name, tc := range map[string]struct {
		err     error
		wantSub []string // substrings the output must contain
		wantNo  []string // substrings the output must NOT contain
	}{
		"nil err":      {err: nil, wantSub: []string{}},
		"plain string": {err: errors.New("plain"), wantSub: []string{"plain"}},
		"zscaler 502 sanitized": {
			err:     errors.New(zscaler),
			wantSub: []string{"502 Bad Gateway", "Server: Zscaler/6.2"},
			wantNo:  []string{"<html>", "Content-Type"},
		},
		"no http response untouched": {
			err:     errors.New("rpc error: code = NotFound desc = pool not found"),
			wantSub: []string{"pool not found"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			got := summarizeRPCErr(tc.err)
			for _, s := range tc.wantSub {
				if !strings.Contains(got, s) {
					t.Errorf("output %q missing substring %q", got, s)
				}
			}
			for _, s := range tc.wantNo {
				if strings.Contains(got, s) {
					t.Errorf("output %q unexpectedly contains %q", got, s)
				}
			}
		})
	}
}
