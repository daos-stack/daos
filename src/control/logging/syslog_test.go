//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//go:build linux
// +build linux

package logging_test

import (
	"fmt"
	"log/syslog"
	"math/rand"
	"os"
	"os/exec"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/logging"
)

func TestSyslogOutput(t *testing.T) {
	journalctl, err := exec.LookPath("journalctl")
	if err != nil {
		t.Log("unable to locate journalctl -- not running this test")
		return
	}
	cmd := exec.Command(journalctl, "--system", "--since", "1 minute ago")
	if err := cmd.Run(); err != nil {
		t.Log("current user does not have permissions to view system log")
		return
	}
	if _, err := syslog.New(syslog.LOG_ALERT, "test"); err != nil {
		t.Logf("unable to connect to syslog: %s -- not running this test", err)
		return
	}

	rand.Seed(time.Now().UnixNano())
	runes := []rune("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
	randString := func(base string, length int) string {
		rs := make([]rune, length)
		for i := range rs {
			rs[i] = runes[rand.Intn(len(runes))]
		}
		return base + " " + string(rs)
	}
	debugStr := randString("DEBUG", 8)
	infoStr := randString("INFO", 8)
	noticeStr := randString("NOTICE", 8)
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
			expected: regexp.MustCompile(fmt.Sprintf(`: %s$`, debugStr))},
		"Debugf": {prio: syslog.LOG_DEBUG, fmtFn: logger.Debugf, fmtFnFmt: fmt.Sprintf("%s: %%d", debugStr), fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(fmt.Sprintf(`%s: 42$`, debugStr))},
		"Info": {prio: syslog.LOG_INFO, fn: logger.Info, fnInput: infoStr,
			expected: regexp.MustCompile(fmt.Sprintf(`: %s$`, infoStr))},
		"Infof": {prio: syslog.LOG_INFO, fmtFn: logger.Infof, fmtFnFmt: fmt.Sprintf("%s: %%d", infoStr), fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(fmt.Sprintf(`%s: 42$`, infoStr))},
		"Notice": {prio: syslog.LOG_INFO, fn: logger.Notice, fnInput: noticeStr,
			expected: regexp.MustCompile(fmt.Sprintf(`: %s$`, noticeStr))},
		"Noticef": {prio: syslog.LOG_INFO, fmtFn: logger.Noticef, fmtFnFmt: fmt.Sprintf("%s: %%d", noticeStr), fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(fmt.Sprintf(`%s: 42$`, noticeStr))},
		"Error": {prio: syslog.LOG_ERR, fn: logger.Error, fnInput: errorStr,
			expected: regexp.MustCompile(fmt.Sprintf(`: %s$`, errorStr))},
		"Errorf": {prio: syslog.LOG_ERR, fmtFn: logger.Errorf, fmtFnFmt: fmt.Sprintf("%s: %%d", errorStr), fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(fmt.Sprintf(`%s: 42$`, errorStr))},
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
			got := strings.TrimSpace(jrnlOut(t, int(tc.prio)))
			if !tc.expected.MatchString(got) {
				t.Fatalf("expected %q to match %s", got, tc.expected)
			}
		})
	}
}
