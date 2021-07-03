//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package txtfmt

import (
	"bytes"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func makeInputLines(in ...string) []string {
	return in
}

func TestTxtFmt_IndentWriter(t *testing.T) {
	for name, tt := range map[string]struct {
		inputLines     []string
		indentLevel    uint
		expectedResult string
	}{
		"empty text": {
			inputLines:     makeInputLines(""),
			expectedResult: "",
		},
		"default indent": {
			inputLines:     makeInputLines("input"),
			expectedResult: "  input",
		},
		"non-default indent": {
			inputLines:     makeInputLines("input"),
			indentLevel:    8,
			expectedResult: "        input",
		},
		"multiple lines, newline separate write": {
			inputLines: makeInputLines("line 1", "\n", "line 2", "\n", "  line 3", "\n"),
			expectedResult: `
  line 1
  line 2
    line 3
`,
		},
		"multiple lines": {
			inputLines: makeInputLines("line 1\n", "line 2\n", "  line 3\n"),
			expectedResult: `
  line 1
  line 2
    line 3
`,
		},
		"multiple lines one write": {
			inputLines: makeInputLines(strings.TrimLeft(`
line 1
line 2
  line 3
`, "\n")),
			expectedResult: `
  line 1
  line 2
    line 3
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var buf bytes.Buffer

			padCount := func(_ *IndentWriter) {}
			if tt.indentLevel != 0 {
				padCount = WithPadCount(tt.indentLevel)
			}

			writer := NewIndentWriter(&buf, padCount)

			for _, line := range tt.inputLines {
				_, err := writer.Write([]byte(line))
				if err != nil {
					t.Fatal(err)
				}
			}

			if diff := cmp.Diff(strings.TrimLeft(tt.expectedResult, "\n"), buf.String()); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
			buf.Reset()
		})
	}
}
