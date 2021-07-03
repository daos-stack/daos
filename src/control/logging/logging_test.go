//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package logging_test

import (
	"bytes"
	"fmt"
	"regexp"
	"runtime"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/logging"
)

type testWrap struct {
	log logging.Logger
}

func (tw *testWrap) Debug(msg string) {
	tw.log.Debug(msg)
}

func (tw *testWrap) Debugf(fmtStr string, args ...interface{}) {
	tw.log.Debugf(fmtStr, args...)
}

func getLineNumber(t *testing.T) int {
	pc := make([]uintptr, 2)
	n := runtime.Callers(2, pc)
	if n > 0 {
		pc = pc[:n]
		frames := runtime.CallersFrames(pc)
		for {
			frame, _ := frames.Next()
			return frame.Line
		}
	}

	t.Fatal("failed to get test line number")
	return 0
}

func TestLogging_DebugOutputDepth(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())

	// not a perfect diff, but good enough for this test
	cmpOut := func(t *testing.T, want, got string) {
		if !strings.Contains(got, want) {
			t.Fatalf("incorrect caller info (-want, +got):\n-%s\n+%s\n", want, got)
		}
	}

	line := getLineNumber(t) + 1
	log.Debug("test 1")
	wanted := fmt.Sprintf("logging_test.go:%d: test 1", line)
	cmpOut(t, wanted, buf.String())
	buf.Reset()

	line = getLineNumber(t) + 1
	log.Debugf("test 2")
	wanted = fmt.Sprintf("logging_test.go:%d: test 2", line)
	cmpOut(t, wanted, buf.String())
	buf.Reset()

	tw := &testWrap{
		log: log,
	}

	line = getLineNumber(t) + 1
	tw.Debug("test 3")
	wanted = fmt.Sprintf("logging_test.go:%d: test 3", line)
	cmpOut(t, wanted, buf.String())
	buf.Reset()

	line = getLineNumber(t) + 1
	tw.Debugf("test 4")
	wanted = fmt.Sprintf("logging_test.go:%d: test 4", line)
	cmpOut(t, wanted, buf.String())
	buf.Reset()
}

func TestLogging_StandardFormat(t *testing.T) {
	logger, buf := logging.NewTestLogger("testPrefix")

	tests := map[string]struct {
		fn        func(string)
		fnInput   string
		fmtFn     func(string, ...interface{})
		fmtFnFmt  string
		fmtFnArgs []interface{}
		expected  *regexp.Regexp
	}{
		"Debug": {fn: logger.Debug, fnInput: "test",
			expected: regexp.MustCompile(`^DEBUG \d{2}:\d{2}:\d{2}\.\d{6} [^:]+:\d+: test\n$`)},
		"Debugf": {fmtFn: logger.Debugf, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(`^DEBUG \d{2}:\d{2}:\d{2}\.\d{6} [^:]+:\d+: test: 42\n$`)},
		"Info": {fn: logger.Info, fnInput: "test",
			expected: regexp.MustCompile(`^testPrefix INFO \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test\n$`)},
		"Infof": {fmtFn: logger.Infof, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(`^testPrefix INFO \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test: 42\n$`)},
		"Error": {fn: logger.Error, fnInput: "test",
			expected: regexp.MustCompile(`^testPrefix ERROR \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test\n$`)},
		"Errorf": {fmtFn: logger.Errorf, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
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

func TestLogging_JSONFormat(t *testing.T) {
	var buf bytes.Buffer

	logger := logging.NewCombinedLogger("testPrefix", &buf).
		WithJSONOutput().
		WithLogLevel(logging.LogLevelDebug)

	tests := map[string]struct {
		fn        func(string)
		fnInput   string
		fmtFn     func(string, ...interface{})
		fmtFnFmt  string
		fmtFnArgs []interface{}
		expected  *regexp.Regexp
	}{
		"Debug": {fn: logger.Debug, fnInput: "test",
			expected: regexp.MustCompile(`^\{\"level\":\"DEBUG\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}[-+Z]\d{0,4}\",\"source\":\"[^:]+:\d+\",\"message\":\"test\"\}\n$`)},
		"Debugf": {fmtFn: logger.Debugf, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(`^\{\"level\":\"DEBUG\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}[-+Z]\d{0,4}\",\"source\":\"[^:]+:\d+\",\"message\":\"test: 42\"\}\n$`)},
		"Info": {fn: logger.Info, fnInput: "test",
			expected: regexp.MustCompile(`^\{\"level\":\"INFO\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[-+Z]\d{0,4}\",\"extra\":\"testPrefix\",\"message\":\"test\"\}\n$`)},
		"Infof": {fmtFn: logger.Infof, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(`^\{\"level\":\"INFO\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[-+Z]\d{0,4}\",\"extra\":\"testPrefix\",\"message\":\"test: 42\"\}\n$`)},
		"Error": {fn: logger.Error, fnInput: "test",
			expected: regexp.MustCompile(`^\{\"level\":\"ERROR\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[-+Z]\d{0,4}\",\"extra\":\"testPrefix\",\"message\":\"test\"\}\n$`)},
		"Errorf": {fmtFn: logger.Errorf, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expected: regexp.MustCompile(`^\{\"level\":\"ERROR\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[-+Z]\d{0,4}\",\"extra\":\"testPrefix\",\"message\":\"test: 42\"\}\n$`)},
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

func TestLogging_MultipleFormats(t *testing.T) {
	var stdBuf bytes.Buffer
	var jsonBuf bytes.Buffer

	logger := logging.NewCombinedLogger("testPrefix", &stdBuf).
		WithLogLevel(logging.LogLevelDebug).
		WithDebugLogger(
			logging.NewDebugLogger(&jsonBuf).WithJSONOutput(),
		).
		WithInfoLogger(
			logging.NewInfoLogger("testPrefix", &jsonBuf).WithJSONOutput(),
		).
		WithErrorLogger(
			logging.NewErrorLogger("testPrefix", &jsonBuf).WithJSONOutput(),
		)

	tests := map[string]struct {
		fn           func(string)
		fnInput      string
		fmtFn        func(string, ...interface{})
		fmtFnFmt     string
		fmtFnArgs    []interface{}
		expectedStd  *regexp.Regexp
		expectedJSON *regexp.Regexp
	}{
		"Debug": {fn: logger.Debug, fnInput: "test",
			expectedStd:  regexp.MustCompile(`^DEBUG \d{2}:\d{2}:\d{2}\.\d{6} [^:]+:\d+: test\n$`),
			expectedJSON: regexp.MustCompile(`^\{\"level\":\"DEBUG\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}[-+Z]\d{0,4}\",\"source\":\"[^:]+:\d+\",\"message\":\"test\"\}\n$`)},
		"Debugf": {fmtFn: logger.Debugf, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expectedStd:  regexp.MustCompile(`^DEBUG \d{2}:\d{2}:\d{2}\.\d{6} [^:]+:\d+: test: 42\n$`),
			expectedJSON: regexp.MustCompile(`^\{\"level\":\"DEBUG\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}[-+Z]\d{0,4}\",\"source\":\"[^:]+:\d+\",\"message\":\"test: 42\"\}\n$`)},
		"Info": {fn: logger.Info, fnInput: "test",
			expectedStd:  regexp.MustCompile(`^testPrefix INFO \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test\n$`),
			expectedJSON: regexp.MustCompile(`^\{\"level\":\"INFO\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[-+Z]\d{0,4}\",\"extra\":\"testPrefix\",\"message\":\"test\"\}\n$`)},
		"Infof": {fmtFn: logger.Infof, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expectedStd:  regexp.MustCompile(`^testPrefix INFO \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test: 42\n$`),
			expectedJSON: regexp.MustCompile(`^\{\"level\":\"INFO\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[-+Z]\d{0,4}\",\"extra\":\"testPrefix\",\"message\":\"test: 42\"\}\n$`)},
		"Error": {fn: logger.Error, fnInput: "test",
			expectedStd:  regexp.MustCompile(`^testPrefix ERROR \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test\n$`),
			expectedJSON: regexp.MustCompile(`^\{\"level\":\"ERROR\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[-+Z]\d{0,4}\",\"extra\":\"testPrefix\",\"message\":\"test\"\}\n$`)},
		"Errorf": {fmtFn: logger.Errorf, fmtFnFmt: "test: %d", fmtFnArgs: []interface{}{42},
			expectedStd:  regexp.MustCompile(`^testPrefix ERROR \d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2} test: 42\n$`),
			expectedJSON: regexp.MustCompile(`^\{\"level\":\"ERROR\",\"time\":\"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[-+Z]\d{0,4}\",\"extra\":\"testPrefix\",\"message\":\"test: 42\"\}\n$`)},
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
			t.Run("JSON", func(t *testing.T) {
				got := jsonBuf.String()
				jsonBuf.Reset()
				if !tc.expectedJSON.MatchString(got) {
					t.Fatalf("expected %q to match %s", got, tc.expectedJSON)
				}
			})
			t.Run("Standard", func(t *testing.T) {
				got := stdBuf.String()
				stdBuf.Reset()
				if !tc.expectedStd.MatchString(got) {
					t.Fatalf("expected %q to match %s", got, tc.expectedStd)
				}
			})
		})
	}
}

func TestLogging_LogLevels(t *testing.T) {
	var buf bytes.Buffer
	logger := logging.NewCombinedLogger("testPrefix", &buf)

	tests := map[string]struct {
		setLevel logging.LogLevel
		fn       func(string)
		fnInput  string
		expected *regexp.Regexp
	}{
		"Disabled-Debug": {setLevel: logging.LogLevelDisabled, fn: logger.Debug, fnInput: "test", expected: regexp.MustCompile(`^$`)},
		"Disabled-Info":  {setLevel: logging.LogLevelDisabled, fn: logger.Info, fnInput: "test", expected: regexp.MustCompile(`^$`)},
		"Disabled-Error": {setLevel: logging.LogLevelDisabled, fn: logger.Error, fnInput: "test", expected: regexp.MustCompile(`^$`)},
		"Debug-Debug":    {setLevel: logging.LogLevelDebug, fn: logger.Debug, fnInput: "test", expected: regexp.MustCompile(`test`)},
		"Debug-Info":     {setLevel: logging.LogLevelDebug, fn: logger.Info, fnInput: "test", expected: regexp.MustCompile(`test`)},
		"Debug-Error":    {setLevel: logging.LogLevelDebug, fn: logger.Error, fnInput: "test", expected: regexp.MustCompile(`test`)},
		"Info-Debug":     {setLevel: logging.LogLevelInfo, fn: logger.Debug, fnInput: "test", expected: regexp.MustCompile(`^$`)},
		"Info-Info":      {setLevel: logging.LogLevelInfo, fn: logger.Info, fnInput: "test", expected: regexp.MustCompile(`test`)},
		"Info-Error":     {setLevel: logging.LogLevelInfo, fn: logger.Error, fnInput: "test", expected: regexp.MustCompile(`test`)},
		"Error-Debug":    {setLevel: logging.LogLevelError, fn: logger.Debug, fnInput: "test", expected: regexp.MustCompile(`^$`)},
		"Error-Info":     {setLevel: logging.LogLevelError, fn: logger.Info, fnInput: "test", expected: regexp.MustCompile(`^$`)},
		"Error-Error":    {setLevel: logging.LogLevelError, fn: logger.Error, fnInput: "test", expected: regexp.MustCompile(`test`)},
	}

	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			logger.SetLevel(tc.setLevel)
			tc.fn(tc.fnInput)
			got := buf.String()
			buf.Reset()
			if !tc.expected.MatchString(got) {
				t.Fatalf("expected %q to match %s", got, tc.expected)
			}
		})
	}
}
