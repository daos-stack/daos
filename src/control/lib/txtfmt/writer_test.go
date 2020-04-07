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
