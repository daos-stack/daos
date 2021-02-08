//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"math"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

func TestSystem_RankYaml(t *testing.T) {
	for name, tc := range map[string]struct {
		in      uint32
		out     *Rank
		wantErr error
	}{
		"good": {
			in:  42,
			out: NewRankPtr(42),
		},
		"bad": {
			in:      uint32(NilRank),
			wantErr: errors.New("out of range"),
		},
		"min": {
			in:  0,
			out: NewRankPtr(0),
		},
		"max": {
			in:  uint32(MaxRank),
			out: NewRankPtr(uint32(MaxRank)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var r Rank

			// we don't really need to test YAML; just
			// verify that our UnmarshalYAML func works.
			unmarshal := func(dest interface{}) error {
				destRank, ok := dest.(*uint32)
				if !ok {
					return errors.New("dest is not *uint32")
				}
				*destRank = tc.in
				return nil
			}
			gotErr := r.UnmarshalYAML(unmarshal)

			common.CmpErr(t, tc.wantErr, gotErr)
			if tc.wantErr != nil {
				return
			}

			if diff := cmp.Diff(tc.out.String(), r.String()); diff != "" {
				t.Fatalf("unexpected value (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_RankStringer(t *testing.T) {
	for name, tc := range map[string]struct {
		r      *Rank
		expStr string
	}{
		"nil": {
			expStr: "NilRank",
		},
		"NilRank": {
			r:      NewRankPtr(uint32(NilRank)),
			expStr: "NilRank",
		},
		"max": {
			r:      NewRankPtr(uint32(MaxRank)),
			expStr: fmt.Sprintf("%d", math.MaxUint32-1),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotStr := tc.r.String()
			if tc.r != nil {
				r := *tc.r
				gotStr = r.String()
			}
			if diff := cmp.Diff(tc.expStr, gotStr); diff != "" {
				t.Fatalf("unexpected String() (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_FmtCast(t *testing.T) {
	for name, tc := range map[string]struct {
		r      Rank
		expStr string
	}{
		"Rank 42": {
			r:      Rank(42),
			expStr: "42",
		},
		"NilRank": {
			r:      NilRank,
			expStr: fmt.Sprintf("%d", math.MaxUint32),
		},
		"MaxRank": {
			r:      MaxRank,
			expStr: fmt.Sprintf("%d", math.MaxUint32-1),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotStr := fmt.Sprintf("%d", tc.r)
			if diff := cmp.Diff(tc.expStr, gotStr); diff != "" {
				t.Fatalf("unexpected String() (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSytem_RankUint32(t *testing.T) {
	for name, tc := range map[string]struct {
		r      *Rank
		expVal uint32
	}{
		"nil": {
			expVal: uint32(NilRank),
		},
		"NilRank": {
			r:      NewRankPtr(uint32(NilRank)),
			expVal: math.MaxUint32,
		},
		"MaxRank": {
			r:      NewRankPtr(uint32(MaxRank)),
			expVal: math.MaxUint32 - 1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotVal := tc.r.Uint32()
			if tc.r != nil {
				r := *tc.r
				gotVal = r.Uint32()
			}
			if diff := cmp.Diff(tc.expVal, gotVal); diff != "" {
				t.Fatalf("unexpected value (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSystem_RankEquals(t *testing.T) {
	for name, tc := range map[string]struct {
		a         *Rank
		b         Rank
		expEquals bool
	}{
		"a is nil": {
			b:         Rank(1),
			expEquals: false,
		},
		"a == b": {
			a:         NewRankPtr(1),
			b:         Rank(1),
			expEquals: true,
		},
		"a != b": {
			a:         NewRankPtr(1),
			b:         Rank(2),
			expEquals: false,
		},
		"MaxRank": {
			a:         NewRankPtr(math.MaxUint32 - 1),
			b:         MaxRank,
			expEquals: true,
		},
		"NilRank": {
			a:         NewRankPtr(math.MaxUint32),
			b:         NilRank,
			expEquals: true,
		},
		"nil == NilRank": {
			b:         NilRank,
			expEquals: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotEquals := tc.a.Equals(tc.b)
			if gotEquals != tc.expEquals {
				t.Fatalf("expected %v.Equals(%v) to be %t, but was %t", tc.a, tc.b, tc.expEquals, gotEquals)
			}
		})
	}
}

func TestSystem_NonPointerRankEquals(t *testing.T) {
	for name, tc := range map[string]struct {
		a         Rank
		b         Rank
		expEquals bool
	}{
		"a == b": {
			a:         Rank(1),
			b:         Rank(1),
			expEquals: true,
		},
		"a != b": {
			a:         Rank(1),
			b:         Rank(2),
			expEquals: false,
		},
		"MaxRank": {
			a:         MaxRank,
			b:         Rank(math.MaxUint32 - 1),
			expEquals: true,
		},
		"NilRank": {
			a:         NilRank,
			b:         Rank(math.MaxUint32),
			expEquals: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotEquals := tc.a.Equals(tc.b)
			if gotEquals != tc.expEquals {
				t.Fatalf("expected %v.Equals(%v) to be %t, but was %t", tc.a, tc.b, tc.expEquals, gotEquals)
			}
		})
	}
}

func TestSystem_RankRemoveFromList(t *testing.T) {
	for name, tc := range map[string]struct {
		r        Rank
		rl       []Rank
		expRanks []Rank
	}{
		"no list": {
			r:        Rank(1),
			rl:       []Rank{},
			expRanks: []Rank{},
		},
		"present": {
			r:        Rank(1),
			rl:       []Rank{Rank(0), Rank(1)},
			expRanks: []Rank{Rank(0)},
		},
		"absent": {
			r:        Rank(1),
			rl:       []Rank{Rank(0), Rank(2)},
			expRanks: []Rank{Rank(0), Rank(2)},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotList := tc.r.RemoveFromList(tc.rl)
			common.AssertEqual(t, tc.expRanks, gotList, name)
		})
	}
}

func TestSystem_RankInList(t *testing.T) {
	for name, tc := range map[string]struct {
		r       Rank
		rl      []Rank
		expBool bool
	}{
		"no list": {
			r:       Rank(1),
			rl:      []Rank{},
			expBool: false,
		},
		"present": {
			r:       Rank(1),
			rl:      []Rank{Rank(0), Rank(1)},
			expBool: true,
		},
		"absent": {
			r:       Rank(1),
			rl:      []Rank{Rank(0), Rank(2)},
			expBool: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotBool := tc.r.InList(tc.rl)
			common.AssertEqual(t, tc.expBool, gotBool, name)
		})
	}
}

func TestSystem_RanksToUint32(t *testing.T) {
	for name, tc := range map[string]struct {
		rl       []Rank
		expRanks []uint32
	}{
		"nil list": {
			rl:       nil,
			expRanks: []uint32{},
		},
		"no list": {
			rl:       []Rank{},
			expRanks: []uint32{},
		},
		"with list": {
			rl:       []Rank{Rank(0), Rank(1), Rank(2)},
			expRanks: []uint32{0, 1, 2},
		},
		"NilRank in list": {
			rl:       []Rank{Rank(0), Rank(2), NilRank},
			expRanks: []uint32{0, 2, 0xffffffff},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotList := RanksToUint32(tc.rl)
			common.AssertEqual(t, tc.expRanks, gotList, name)
		})
	}
}

func TestSystem_RanksFromUint32(t *testing.T) {
	for name, tc := range map[string]struct {
		expRanks []Rank
		rl       []uint32
	}{
		"nil list": {
			rl:       nil,
			expRanks: []Rank{},
		},
		"no list": {
			rl:       []uint32{},
			expRanks: []Rank{},
		},
		"with list": {
			rl:       []uint32{0, 1, 2},
			expRanks: []Rank{Rank(0), Rank(1), Rank(2)},
		},
		"NilRank in list": {
			rl:       []uint32{0, 2, 0xffffffff},
			expRanks: []Rank{Rank(0), Rank(2), NilRank},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotList := RanksFromUint32(tc.rl)
			common.AssertEqual(t, tc.expRanks, gotList, name)
		})
	}
}

func TestSystem_TestRankMembership(t *testing.T) {
	for name, tc := range map[string]struct {
		members    []Rank
		test       []Rank
		expMissing []Rank
	}{
		"empty": {},
		"no members": {
			test:       []Rank{1},
			expMissing: []Rank{1},
		},
		"empty test": {
			members: []Rank{0},
		},
		"no missing": {
			members: []Rank{0},
			test:    []Rank{0},
		},
		"one missing": {
			members:    []Rank{0},
			test:       []Rank{1},
			expMissing: []Rank{1},
		},
		"overlap": {
			members:    []Rank{0, 1},
			test:       []Rank{1, 2},
			expMissing: []Rank{2},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotMissing := CheckRankMembership(tc.members, tc.test)

			if diff := cmp.Diff(tc.expMissing, gotMissing); diff != "" {
				t.Fatalf("unexpected missing ranks (-want, +got):\n%s\n", diff)
			}
		})
	}
}
