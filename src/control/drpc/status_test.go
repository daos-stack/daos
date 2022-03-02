//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package drpc_test

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
)

func TestDrpc_Status(t *testing.T) {
	for name, tc := range map[string]struct {
		in     int32
		expErr error
	}{
		"rc 0": {
			expErr: drpc.DaosSuccess,
		},
		"-1007": {
			in:     -1007,
			expErr: drpc.DaosNoSpace,
		},
		"-424242": {
			in:     -424242,
			expErr: errors.New("DER_UNKNOWN"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			ds := drpc.DaosStatus(tc.in)
			test.CmpErr(t, tc.expErr, ds)
		})
	}
}

func TestDrpc_Error(t *testing.T) {
	// Light test to make sure the error stringer works as expected.
	for ds, expStr := range map[drpc.DaosStatus]string{
		drpc.DaosSuccess:        "DER_SUCCESS(0): Success",
		drpc.DaosProtocolError:  "DER_PROTO(-1014): Incompatible protocol",
		drpc.DaosNotReplica:     "DER_NOTREPLICA(-2020): Not a service replica",
		drpc.DaosStatus(424242): "DER_UNKNOWN(424242): Unknown error code 424242",
	} {
		t.Run(expStr, func(t *testing.T) {
			test.AssertEqual(t, expStr, ds.Error(), "not equal")
		})
	}
}
