//
// (C) Copyright 2020-2021 Intel Corporation.
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

package txtfmt

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestEntityFormatter(t *testing.T) {
	for name, tc := range map[string]struct {
		title          string
		attrs          []TableRow
		expectedResult string
	}{
		"normal": {
			title: "title",
			attrs: []TableRow{
				{"a": "b"},
				{"c": "d"},
				{"bananas": "grapes"},
			},
			expectedResult: `
title
-----
  a       : b       
  c       : d       
  bananas : grapes  
`,
		},
		"empty title": {
			attrs: []TableRow{
				{"a": "b"},
			},
			expectedResult: "  a : b \n",
		},
		"empty attrs": {
			title: "empty",
			expectedResult: `
empty
-----
`,
		},
		"long title": {
			title: "long title is long",
			attrs: []TableRow{
				{"a": "b"},
				{"foo": "bar"},
			},
			expectedResult: `
long title is long
------------------
  a   : b   
  foo : bar 
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := FormatEntity(tc.title, tc.attrs)

			if diff := cmp.Diff(strings.TrimLeft(tc.expectedResult, "\n"), result); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}
