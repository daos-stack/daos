//
// (C) Copyright 2019-2020 Intel Corporation.
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

package hostlist_test

import (
	"errors"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

func TestHostList_CreateNumericList(t *testing.T) {
	for name, tc := range map[string]struct {
		startList    string
		expRawOut    string
		expUniqOut   string
		expUniqCount int
		expErr       error
	}{
		"letters in numeric list": {
			startList: "node[1-128]",
			expErr:    errors.New("unexpected alphabetic character(s)"),
		},
		"ranges only": {
			startList:    "[0-1,3-5,3]",
			expRawOut:    "[0-1,3-5,3]",
			expUniqOut:   "[0-1,3-5]",
			expUniqCount: 5,
		},
	} {
		t.Run(name, func(t *testing.T) {
			hl, gotErr := hostlist.CreateNumericList(tc.startList)
			if gotErr != nil {
				t.Log(gotErr.Error())
			}
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expRawOut, hl.String())
			hl.Uniq()
			cmpOut(t, tc.expUniqOut, hl.String())

			gotCount := hl.Count()
			if gotCount != tc.expUniqCount {
				t.Fatalf("expected count to be %d; got %d", tc.expUniqCount, gotCount)
			}
		})
	}
}

func TestHostSet_CreateNumericSet(t *testing.T) {
	for name, tc := range map[string]struct {
		startList string
		expOut    string
		expCount  int
		expErr    error
	}{
		"letters in numeric list": {
			startList: "node2-1,node1-2.suffix1,node1-[45,47].suffix2,node3,node1-3",
			expErr:    errors.New("unexpected alphabetic character(s)"),
		},
		"ranges only": {
			startList: "[1-128,2,4,8,6,32,64,128]",
			expOut:    "[1-128]",
			expCount:  128,
		},
		"whitespace in numeric list": {
			startList: "[0, 5]",
			expErr:    errors.New("unexpected whitespace character(s)"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			hs, gotErr := hostlist.CreateNumericSet(tc.startList)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expOut, hs.String())

			gotCount := hs.Count()
			if gotCount != tc.expCount {
				t.Fatalf("\nexpected count to be %d; got %d", tc.expCount, gotCount)
			}
		})
	}
}
