//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
		t.Skip("unable to locate journalctl -- not running this test")
	}
	cmd := exec.Command(journalctl, "--system")
	if err := cmd.Run(); err != nil {
		t.Skip("current user does not have permissions to view system log")
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
		time.Sleep(10 * time.Millisecond) // Give it time to settle
		cmd := exec.Command(journalctl,
			fmt.Sprintf("_PID=%d", os.Getpid()),
			fmt.Sprintf("PRIORITY=%d", prio),
		)
		out, err := cmd.Output()
		if err != nil {
			var stderr string
			if ee, ok := err.(*exec.ExitError); ok {
				stderr = string(ee.Stderr)
			}
			t.Fatalf("error: %s\nSTDOUT: %s\nSTDERR: %s", err, out, stderr)
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
