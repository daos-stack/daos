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
// +build linux

package logging_test

import (
	"fmt"
	"log/syslog"
	"math/rand"
	"os"
	"os/exec"
	"regexp"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/logging"
)

func TestSyslogOutput(t *testing.T) {
	journalctl, err := exec.LookPath("journalctl")
	if err != nil {
		t.Skip("Unable to locate journalctl -- not running this test")
	}

	rand.Seed(time.Now().UnixNano())
	runes := []rune("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
	randString := func(base string, length int) string {
		rs := make([]rune, length)
		for i := range rs {
			rs[i] = runes[rand.Intn(len(runes))]
		}
		return base + string(rs)
	}
	debugStr := randString("DEBUG", 8)
	infoStr := randString("INFO", 8)
	errorStr := randString("ERROR", 8)

	logger := logging.NewStdoutLogger("testPrefix").
		WithSyslogOutput().
		WithLogLevel(logging.LogLevelDebug)

	tests := map[string]struct {
		prio      syslog.Priority
		fn        func(string)
		fnInput   string
		fmtFn     func(string, ...interface{})
		fmtFnFmt  string
		fmtFnArgs []interface{}
		expected  *regexp.Regexp
	}{
		"Debug": {prio: syslog.LOG_DEBUG, fn: logger.Debug, fnInput: debugStr,
			expected: regexp.MustCompile(fmt.Sprintf(`\n[A-Za-z]{3} \d{2} \d{2}:\d{2}:\d{2} [\w\.\-]+ [^:]+: \d{2}:\d{2}:\d{2}\.\d{6} [^:]+:\d+: %s\n`, debugStr))},
		"Debugf": {prio: syslog.LOG_DEBUG, fmtFn: logger.Debugf, fmtFnFmt: fmt.Sprintf("%s: %%d", debugStr), fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(fmt.Sprintf(`\n[A-Za-z]{3} \d{2} \d{2}:\d{2}:\d{2} [\w\.\-]+ [^:]+: \d{2}:\d{2}:\d{2}\.\d{6} [^:]+:\d+: %s: 42\n`, debugStr))},
		"Info": {prio: syslog.LOG_INFO, fn: logger.Info, fnInput: infoStr,
			expected: regexp.MustCompile(fmt.Sprintf(`\n[A-Za-z]{3} \d{2} \d{2}:\d{2}:\d{2} [\w\.\-]+ [^:]+: %s\n`, infoStr))},
		"Infof": {prio: syslog.LOG_INFO, fmtFn: logger.Infof, fmtFnFmt: fmt.Sprintf("%s: %%d", infoStr), fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(fmt.Sprintf(`\n[A-Za-z]{3} \d{2} \d{2}:\d{2}:\d{2} [\w\.\-]+ [^:]+: %s: 42\n`, infoStr))},
		"Error": {prio: syslog.LOG_ERR, fn: logger.Error, fnInput: errorStr,
			expected: regexp.MustCompile(fmt.Sprintf(`\n[A-Za-z]{3} \d{2} \d{2}:\d{2}:\d{2} [\w\.\-]+ [^:]+: %s\n`, errorStr))},
		"Errorf": {prio: syslog.LOG_ERR, fmtFn: logger.Errorf, fmtFnFmt: fmt.Sprintf("%s: %%d", errorStr), fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(fmt.Sprintf(`\n[A-Za-z]{3} \d{2} \d{2}:\d{2}:\d{2} [\w\.\-]+ [^:]+: %s: 42\n`, errorStr))},
	}

	jrnlOut := func(t *testing.T, prio int) string {
		t.Helper()

		time.Sleep(10 * time.Millisecond) // Give it time to settle
		cmd := exec.Command(journalctl,
			fmt.Sprintf("_PID=%d", os.Getpid()),
			fmt.Sprintf("PRIORITY=%d", prio),
		)
		out, err := cmd.Output()
		if err != nil {
			t.Fatal(err)
		}
		return string(out)
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
			got := jrnlOut(t, int(tc.prio))
			if !tc.expected.MatchString(got) {
				t.Fatalf("expected %q to match %s", got, tc.expected)
			}
		})
	}
}
