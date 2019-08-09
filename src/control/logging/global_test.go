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
package logging_test

import (
	"bytes"
	"regexp"
	"testing"

	"github.com/daos-stack/daos/src/control/logging"
)

func TestPackageGlobalFunctions(t *testing.T) {
	var buf bytes.Buffer

	logging.SetLevel(logging.LogLevelDebug)
	logging.SetLogger(
		logging.NewCombinedLogger("testPrefix", &buf),
	)

	tests := map[string]struct {
		fn        func(string)
		fnInput   string
		fmtFn     func(string, ...interface{})
		fmtFnFmt  string
		fmtFnArgs []interface{}
		expected  *regexp.Regexp
	}{
		"Debug": {fn: logging.Debug, fnInput: "test",
			expected: regexp.MustCompile(`^DEBUG \d{2}:\d{2}:\d{2}\.\d{6} [^:]+:\d+: test\n$`)},
		"Debugf": {fmtFn: logging.Debugf, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(`^DEBUG \d{2}:\d{2}:\d{2}\.\d{6} [^:]+:\d+: test: 42\n$`)},
		"Info": {fn: logging.Info, fnInput: "test",
			expected: regexp.MustCompile(`^testPrefix INFO \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test\n$`)},
		"Infof": {fmtFn: logging.Infof, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(`^testPrefix INFO \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test: 42\n$`)},
		"Error": {fn: logging.Error, fnInput: "test",
			expected: regexp.MustCompile(`^testPrefix ERROR \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test\n$`)},
		"Errorf": {fmtFn: logging.Errorf, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(`^testPrefix ERROR \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test: 42\n$`)},
	}

	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			switch {
			case tc.fn != nil:
				tc.fn(tc.fnInput)
			case tc.fmtFn != nil:
				tc.fmtFn(tc.fmtFnFmt, tc.fmtFnArgs...)
			default:
				t.Fatal("no test function defined")
			}
			got := buf.String()
			buf.Reset()
			if !tc.expected.MatchString(got) {
				t.Fatalf("expected %q to match %s", got, tc.expected)
			}
		})
	}
}
