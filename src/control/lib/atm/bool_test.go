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

package atm_test

import (
	"testing"

	"github.com/daos-stack/daos/src/control/lib/atm"
)

func TestAtomicBool(t *testing.T) {
	for name, tc := range map[string]struct {
		start      bool
		op         string
		expEnd     bool
		expChanged bool
	}{
		"true-IsTrue": {
			start:  true,
			op:     "IsTrue",
			expEnd: true,
		},
		"true-IsFalse": {
			start:  true,
			op:     "IsFalse",
			expEnd: false,
		},
		"false-IsTrue": {
			start:  false,
			op:     "IsTrue",
			expEnd: false,
		},
		"false-IsFalse": {
			start:  false,
			op:     "IsFalse",
			expEnd: true,
		},
		"true-SetTrue": {
			start:  true,
			op:     "SetTrue",
			expEnd: true,
		},
		"true-SetFalse": {
			start:  true,
			op:     "SetFalse",
			expEnd: false,
		},
		"false-SetTrue": {
			start:  false,
			op:     "SetTrue",
			expEnd: true,
		},
		"false-SetFalse": {
			start:  false,
			op:     "SetFalse",
			expEnd: false,
		},
	} {
		cmpBool := func(t *testing.T, expected, actual bool) {
			t.Helper()

			if actual != expected {
				t.Fatalf("expected %t; got %t", expected, actual)
			}
		}

		t.Run(name, func(t *testing.T) {
			b := atm.NewBool(tc.start)

			switch tc.op {
			case "SetTrue":
				b.SetTrue()
				cmpBool(t, b.Load(), tc.expEnd)
			case "IsTrue":
				cmpBool(t, b.IsTrue(), tc.expEnd)
			case "SetFalse":
				b.SetFalse()
				cmpBool(t, b.Load(), tc.expEnd)
			case "IsFalse":
				cmpBool(t, b.IsFalse(), tc.expEnd)
			default:
				t.Fatalf("unhandled op %q", tc.op)
			}
		})
	}
}
