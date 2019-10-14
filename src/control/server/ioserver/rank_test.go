//
// (C) Copyright 2019 Intel Corporation.
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

package ioserver

import (
	"testing"

	"github.com/pkg/errors"
)

func TestRankYaml(t *testing.T) {
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
			wantErr: errors.New("nope"),
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
			err := r.UnmarshalYAML(unmarshal)

			if err != tc.wantErr {
				if err == nil && tc.wantErr != nil {
					t.Fatal("expected error; got nil")
				}
				if tc.wantErr == nil && err != nil {
					t.Fatalf("expected no error; got %s", err)
				}
			}
			if err == nil && r.String() != tc.out.String() {
				t.Fatalf("expected %q; got %q", tc.out, r)
			}
		})
	}
}

func TestRankEquals(t *testing.T) {
	for name, tc := range map[string]struct {
		a      *Rank
		b      *Rank
		equals bool
	}{
		"both nil": {
			equals: false,
		},
		"a is nil": {
			b:      NewRankPtr(1),
			equals: false,
		},
		"b is nil": {
			a:      NewRankPtr(1),
			equals: false,
		},
		"a == b": {
			a:      NewRankPtr(1),
			b:      NewRankPtr(1),
			equals: true,
		},
		"a != b": {
			a:      NewRankPtr(1),
			b:      NewRankPtr(2),
			equals: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			got := tc.a.Equals(tc.b)
			if got != tc.equals {
				t.Fatalf("expected %v.Equals(%v) to be %t, but was %t", tc.a, tc.b, tc.equals, got)
			}
		})
	}
}
