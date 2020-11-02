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

package system

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

func TestSystem_RankSet(t *testing.T) {
	for name, tc := range map[string]struct {
		ranks    string
		addRanks []Rank
		expOut   string
		expCount int
		expRanks []Rank
		expErr   error
	}{
		"empty start list": {
			ranks:    "",
			expOut:   "",
			expCount: 0,
			expRanks: []Rank{},
		},
		"invalid with hostnames": {
			ranks:  "node2-1,node1-2.suffix1,node1-[45,47].suffix2,node3,node1-3",
			expErr: errors.New("unexpected alphabetic character(s)"),
		},
		"simple ranged rank list": {
			ranks:    "0-10",
			expOut:   "0-10",
			expCount: 11,
			expRanks: []Rank{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
		},
		"deranged rank list": {
			ranks:    "1,2,3,5,6,8,10,10,1",
			expOut:   "1-3,5-6,8,10",
			expCount: 7,
			expRanks: []Rank{1, 2, 3, 5, 6, 8, 10},
		},
		"ranged rank list": {
			ranks:    "2,3,10-19,0-9",
			addRanks: []Rank{30, 32},
			expOut:   "0-19,30,32",
			expCount: 22,
			expRanks: []Rank{
				0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
				10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
				30, 32,
			},
		},
		"adding to empty list": {
			ranks:    "",
			addRanks: []Rank{30, 32},
			expOut:   "30,32",
			expCount: 2,
			expRanks: []Rank{30, 32},
		},
		"invalid list with spaces": {
			ranks:  "1, 5",
			expErr: errors.New("unexpected whitespace character(s)"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			rs, gotErr := CreateRankSet(tc.ranks)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			for _, r := range tc.addRanks {
				rs.Add(r)
			}

			if diff := cmp.Diff(tc.expOut, rs.String()); diff != "" {
				t.Fatalf("unexpected value (-want, +got):\n%s\n", diff)
			}

			gotCount := rs.Count()
			if gotCount != tc.expCount {
				t.Fatalf("\nexpected count to be %d; got %d", tc.expCount, gotCount)
			}

			ranks := rs.Ranks()
			if diff := cmp.Diff(tc.expRanks, ranks); diff != "" {
				t.Fatalf("unexpected ranks (-want, +got):\n%s\n", diff)
			}

		})
	}
}
