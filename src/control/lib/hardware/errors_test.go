//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/pkg/errors"
)

func TestHardware_IsUnsupportedFabric(t *testing.T) {
	for name, tc := range map[string]struct {
		err       error
		expResult bool
	}{
		"nil": {},
		"true": {
			err:       ErrUnsupportedFabric("dontcare"),
			expResult: true,
		},
		"false": {
			err: errors.New("something else"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, IsUnsupportedFabric(tc.err), "")
		})
	}
}

func TestHardware_IsProviderNotOnDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		err       error
		expResult bool
	}{
		"nil": {},
		"true": {
			err:       ErrProviderNotOnDevice("dontcare", "dontcare"),
			expResult: true,
		},
		"false": {
			err: errors.New("something else"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, IsProviderNotOnDevice(tc.err), "")
		})
	}
}
