//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common_test

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
)

type (
	testIface interface {
		test()
	}
	testImp struct{}
)

func (ti *testImp) test() {}

func TestCommon_InterfaceIsNil(t *testing.T) {
	for name, tc := range map[string]struct {
		in      interface{}
		expBool bool
	}{
		"nil type": {
			in:      nil,
			expBool: true,
		},
		"nil value": {
			in:      (testIface)(nil),
			expBool: true,
		},
		"non-nil value": {
			in:      (testIface)(&testImp{}),
			expBool: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotBool := common.InterfaceIsNil(tc.in)
			if gotBool != tc.expBool {
				t.Fatalf("expected %t; got %t", tc.expBool, gotBool)
			}
		})
	}
}
