//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"net"
	"testing"

	"github.com/pkg/errors"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/security"
)

func TestControl_connErrToFault(t *testing.T) {
	testTarget := "10.0.0.7"
	for name, tc := range map[string]struct {
		st     *status.Status
		expErr error
	}{
		"connection refused": {
			st:     status.New(codes.Unavailable, "connection refused"),
			expErr: FaultConnectionRefused(testTarget),
		},
		"no route to host": {
			st:     status.New(codes.Unavailable, "no route to host"),
			expErr: FaultConnectionNoRoute(testTarget),
		},
		"nonexistent host": {
			st:     status.New(codes.Unavailable, "no such host"),
			expErr: FaultConnectionBadHost(testTarget),
		},
		"i/o timeout": {
			st:     status.New(codes.Unavailable, "i/o timeout"),
			expErr: FaultConnectionTimedOut(testTarget),
		},
		"certificate has expired": {
			st:     status.New(codes.Unavailable, "certificate has expired"),
			expErr: security.FaultInvalidCert(errors.New("certificate has expired")),
		},
		"connection closed": {
			st:     status.New(codes.Unavailable, "connection closed"),
			expErr: FaultConnectionClosed(testTarget),
		},
		"transport is closing": {
			st:     status.New(codes.Unavailable, "transport is closing"),
			expErr: FaultConnectionClosed(testTarget),
		},
		"closed the connection": {
			st:     status.New(codes.Unavailable, "closed the connection"),
			expErr: FaultConnectionClosed(testTarget),
		},
		"net.ErrClosed": {
			st:     status.New(codes.Unavailable, net.ErrClosed.Error()),
			expErr: FaultConnectionClosed(testTarget),
		},
		"misc error": {
			st:     status.New(codes.Unavailable, "foobar"),
			expErr: errors.New("foobar"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := connErrToFault(tc.st, testTarget)

			test.CmpErr(t, tc.expErr, err)
		})
	}
}
