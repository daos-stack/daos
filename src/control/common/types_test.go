//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
