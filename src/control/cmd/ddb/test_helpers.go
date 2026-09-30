//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

package main

import (
	"bytes"
	"io"
	"os"
	"testing"
)

type ddbTestErr string

func (dte ddbTestErr) Error() string {
	return string(dte)
}

const (
	errUnknownCmd = ddbTestErr("Unknown command:")
)

// newTestContext creates a fresh DdbContext for use in tests, resetting all
// stub variables to their zero values to ensure test isolation.
func newTestContext(t *testing.T) *DdbContext {
	t.Helper()
	resetDdbStubs()
	return &DdbContext{}
}

// captureStdout replaces os.Stdout with a pipe, runs fn, restores os.Stdout,
// and returns the captured output along with any error from fn.
func captureStdout(fn func() error) (output string, err error) {
	var result bytes.Buffer
	r, w, _ := os.Pipe()
	done := make(chan struct{})
	go func() {
		_, _ = io.Copy(&result, r)
		close(done)
	}()
	stdout := os.Stdout
	os.Stdout = w
	// Deferred so cleanup and output capture always run, even if fn() exits
	// via runtime.Goexit() (e.g., t.Fatalf called from a stub via test.CmpAny).
	defer func() {
		w.Close()
		<-done
		os.Stdout = stdout
		output = result.String()
	}()

	err = fn()
	return
}

// runCmdToStdout calls parseOpts with the given args and captures stdout
// output. Returns the parsed options, stdout output, and error.
func runCmdToStdout(ctx *DdbContext, args []string) (cliOptions, string, error) {
	var opts cliOptions
	stdout, err := captureStdout(func() error {
		var e error
		opts, _, e = parseOpts(args, ctx)
		return e
	})

	return opts, stdout, err
}
