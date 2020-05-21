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
)

func TestHostList_CreateNumber(t *testing.T) {
	for name, tc := range map[string]struct {
		startList    string
		expRawOut    string
		expUniqOut   string
		expUniqCount int
		expErr       error
	}{
		"simple": {
			startList:    "node[1-128]",
			expRawOut:    "node[1-128]",
			expUniqOut:   "node[1-128]",
			expUniqCount: 128,
		},
		"complex": {
			startList:    "node2-1,node1-2,node1-[45,47],node3,node1-3",
			expRawOut:    "node2-1,node1-[2,45,47],node3,node1-3",
			expUniqOut:   "node3,node1-[2-3,45,47],node2-1",
			expUniqCount: 6,
		},
		"no hostname": {
			startList:    "[0-1],[3-5]",
			expRawOut:    "0-1,3-5",
			expUniqOut:   "0-1,3-5",
			expUniqCount: 5,
		},
	} {
		t.Run(name, func(t *testing.T) {
			hl, gotErr := Create(tc.startList)
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
