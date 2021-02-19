//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package atm_test

import (
	"testing"

	"github.com/daos-stack/daos/src/control/lib/atm"
)

func TestAtomicBool_ZeroValue(t *testing.T) {
	var b atm.Bool

	if b.Load() {
		t.Fatal("atm.Bool zero value was not false")
	}

	b.Store(true)

	if !b.Load() {
		t.Fatal("atm.Bool didn't store true")
	}
}

func TestAtomicBool_Ops(t *testing.T) {
	for name, tc := range map[string]struct {
		start      bool
		op         string
		val        bool
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
		"true-Store(true)": {
			start:  true,
			val:    true,
			op:     "Store",
			expEnd: true,
		},
		"true-Store(false)": {
			start:  true,
			val:    false,
			op:     "Store",
			expEnd: false,
		},
		"false-Store(true)": {
			start:  false,
			val:    true,
			op:     "Store",
			expEnd: true,
		},
		"false-Store(false)": {
			start:  false,
			val:    false,
			op:     "Store",
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
			case "Store":
				b.Store(tc.val)
				cmpBool(t, b.Load(), tc.expEnd)
			default:
				t.Fatalf("unhandled op %q", tc.op)
			}
		})
	}
}
