//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package drpc_test

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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
			common.CmpErr(t, tc.expErr, ds)
		})
	}
}
