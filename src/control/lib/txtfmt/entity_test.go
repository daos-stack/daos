//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
