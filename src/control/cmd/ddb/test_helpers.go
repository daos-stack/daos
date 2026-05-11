//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

package main

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"reflect"
	"testing"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/pkg/errors"
)

type ddbTestErr string

func (dte ddbTestErr) Error() string {
	return string(dte)
}

const (
	errUnknownCmd = ddbTestErr(grumbleUnknownCmdErr)
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
func captureStdout(fn func() error) (string, error) {
	var result bytes.Buffer
	r, w, _ := os.Pipe()
	done := make(chan struct{})
	go func() {
		_, _ = io.Copy(&result, r)
		close(done)
	}()
	stdout := os.Stdout
	defer func() { os.Stdout = stdout }()
	os.Stdout = w

	err := fn()
	w.Close()
	<-done

	return result.String(), err
}

// runCmdToStdout calls parseOpts with the given args and captures stdout
// output. errHelpRequested is treated as a non-error (consistent with main()).
// Returns the parsed options, stdout output, and error.
func runCmdToStdout(ctx *DdbContext, args []string) (cliOptions, string, error) {
	var opts cliOptions
	stdout, err := captureStdout(func() error {
		var e error
		opts, _, e = parseOpts(args, ctx)
		return e
	})

	if errors.Is(err, errHelpRequested) {
		return opts, stdout, nil
	}
	return opts, stdout, err
}

// runMainFlow simulates the main() execution flow without calling os.Exit.
// It calls parseOpts, handles the version flag, then calls run().
// errHelpRequested is treated as a non-error (consistent with main()).
// Returns stdout output and any error.
func runMainFlow(ctx *DdbContext, args []string) (string, error) {
	stdout, err := captureStdout(func() error {
		opts, parser, e := parseOpts(args, ctx)
		if errors.Is(e, errHelpRequested) {
			return nil
		}
		if e != nil {
			return e
		}

		if opts.Version {
			fmt.Printf("ddb version %s\n", build.DaosVersion)
			return nil
		}

		log := logging.NewCommandLineLogger()
		return run(ctx, log, opts, parser)
	})
	return stdout, err
}

func isArgEqual(want interface{}, got interface{}, wantName string) error {
	if reflect.DeepEqual(want, got) {
		return nil
	}

	return errors.New(fmt.Sprintf("Unexpected %s argument: wanted '%+v', got '%+v'", wantName, want, got))
}
