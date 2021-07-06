//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package txtfmt

import (
	"strings"
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
			titles: []string{"One"},
			table:  []TableRow{{"One": "1"}},
			expectedResult: `
One 
--- 
1   
`,
		},
		"multi-row": {
			titles: []string{"One"},
			table:  []TableRow{{"One": "1"}, {"One": "2"}, {"One": "3"}},
			expectedResult: `
One 
--- 
1   
2   
3   
`,
		},
		"multi-column": {
			titles: []string{"One", "Two", "Three"},
			table:  []TableRow{{"One": "1", "Two": "2", "Three": "3"}, {"One": "4", "Two": "5", "Three": "6"}},
			expectedResult: `
One Two Three 
--- --- ----- 
1   2   3     
4   5   6     
`,
		},
		"empty table": {
			titles: []string{"One"},
			table:  []TableRow{},
			expectedResult: `
One 
--- 
`,
		},
		"no matches for titles": {
			titles: []string{"One", "Two"},
			table:  []TableRow{{}},
			expectedResult: `
One  Two  
---  ---  
None None 
`,
		},
		"matches for some titles": {
			titles: []string{"One", "Two"},
			table:  []TableRow{{"One": "1"}, {"Two": "2"}},
			expectedResult: `
One  Two  
---  ---  
1    None 
None 2    
`,
		},
		"extra keys ignored": {
			titles: []string{"One", "Three"},
			table:  []TableRow{{"One": "1", "Two": "2", "Three": "3", "Four": "4"}},
			expectedResult: `
One Three 
--- ----- 
1   3     
`,
		},
		"tabbing for long string": {
			titles: []string{"One", "Two"},
			table:  []TableRow{{"One": "1", "Two": "2"}, {"One": "too darn long"}},
			expectedResult: `
One           Two  
---           ---  
1             2    
too darn long None 
`,
		},
		"8-char field should be padded": {
			titles: []string{"Hosts", "SCM Total", "NVMe Total"},
			table: []TableRow{{
				"Hosts":      "wolf-118",
				"SCM Total":  "5.79TB (2 namespaces)",
				"NVMe Total": "1.46TB (2 controllers)",
			}},
			expectedResult: `
Hosts    SCM Total             NVMe Total             
-----    ---------             ----------             
wolf-118 5.79TB (2 namespaces) 1.46TB (2 controllers) 
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := NewTableFormatter(tt.titles...)

			result := f.Format(tt.table)

			if diff := cmp.Diff(strings.TrimLeft(tt.expectedResult, "\n"), result); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}
