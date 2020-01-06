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

package txtfmt

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestNewTableFormatter_NoTitles(t *testing.T) {
	f := NewTableFormatter()
	if f.writer == nil {
		t.Fatal("no tabwriter set!")
	}
	if len(f.titles) != 0 {
		t.Fatalf("non-empty column list, len=%d", len(f.titles))
	}
}

func TestNewTableFormatter_WithTitles(t *testing.T) {
	titles := []string{"One", "Two", "Three"}
	f := NewTableFormatter(titles...)
	if f.writer == nil {
		t.Fatal("no tabwriter set!")
	}
	if diff := cmp.Diff(titles, f.titles); diff != "" {
		t.Fatalf("unexpected column titles (-want, +got):\n%s\n", diff)
	}
}

func TestTableFormatter_Init(t *testing.T) {
	f := &TableFormatter{}
	f.Init()
	if f.writer == nil {
		t.Fatal("no tabwriter set!")
	}
}

func TestTableFormatter_SetColumnTitles(t *testing.T) {
	for name, tt := range map[string]struct {
		startingTitles []string
		titles         []string
		expectedTitles []string
	}{
		"nil": {
			startingTitles: []string{"some", "existing", "titles"},
			titles:         nil,
			expectedTitles: []string{},
		},
		"empty": {
			startingTitles: []string{"some", "titles"},
			titles:         []string{},
			expectedTitles: []string{},
		},
		"setting from empty": {
			titles:         []string{"new", "titles"},
			expectedTitles: []string{"new", "titles"},
		},
		"changing from existing": {
			startingTitles: []string{"some", "existing", "titles"},
			titles:         []string{"new", "titles"},
			expectedTitles: []string{"new", "titles"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := &TableFormatter{titles: tt.startingTitles}

			f.SetColumnTitles(tt.titles...)

			if diff := cmp.Diff(tt.expectedTitles, f.titles); diff != "" {
				t.Fatalf("unexpected titles (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestTableFormatter_Format(t *testing.T) {
	for name, tt := range map[string]struct {
		titles         []string
		table          []TableRow
		expectedResult string
	}{
		"no titles": {
			table:          []TableRow{{"One": "1", "Two": "2"}},
			expectedResult: "",
		},
		"single row": {
			titles:         []string{"One"},
			table:          []TableRow{{"One": "1"}},
			expectedResult: "One\t\n---\t\n1\t\n",
		},
		"multi-row": {
			titles:         []string{"One"},
			table:          []TableRow{{"One": "1"}, {"One": "2"}, {"One": "3"}},
			expectedResult: "One\t\n---\t\n1\t\n2\t\n3\t\n",
		},
		"multi-column": {
			titles:         []string{"One", "Two", "Three"},
			table:          []TableRow{{"One": "1", "Two": "2", "Three": "3"}, {"One": "4", "Two": "5", "Three": "6"}},
			expectedResult: "One\tTwo\tThree\t\n---\t---\t-----\t\n1\t2\t3\t\n4\t5\t6\t\n",
		},
		"empty table": {
			titles:         []string{"One"},
			table:          []TableRow{},
			expectedResult: "One\t\n---\t\n",
		},
		"no matches for titles": {
			titles:         []string{"One", "Two"},
			table:          []TableRow{{}},
			expectedResult: "One\tTwo\t\n---\t---\t\nNone\tNone\t\n",
		},
		"matches for some titles": {
			titles:         []string{"One", "Two"},
			table:          []TableRow{{"One": "1"}, {"Two": "2"}},
			expectedResult: "One\tTwo\t\n---\t---\t\n1\tNone\t\nNone\t2\t\n",
		},
		"extra keys ignored": {
			titles:         []string{"One", "Three"},
			table:          []TableRow{{"One": "1", "Two": "2", "Three": "3", "Four": "4"}},
			expectedResult: "One\tThree\t\n---\t-----\t\n1\t3\t\n",
		},
		"tabbing for long string": {
			titles:         []string{"One", "Two"},
			table:          []TableRow{{"One": "1", "Two": "2"}, {"One": "too darn long"}},
			expectedResult: "One\t\tTwo\t\n---\t\t---\t\n1\t\t2\t\ntoo darn long\tNone\t\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := NewTableFormatter(tt.titles...)

			result := f.Format(tt.table)

			if result != tt.expectedResult {
				t.Fatalf("want: '%s', got: '%s'", tt.expectedResult, result)
			}
		})
	}
}
