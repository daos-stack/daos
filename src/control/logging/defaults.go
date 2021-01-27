//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package logging

import (
	"io"
	"os"
)

const (
	DefaultLogLevel = LogLevelInfo
	logOutputDepth  = 3
	emptyLogFlags   = 0
)

// NewCommandLineLogger returns a logger configured
// to send non-error output to stdout and error
// output to stderr. The output format is suitable
// for command line utilities which don't want output
// to include timestamps and filenames.
func NewCommandLineLogger() *LeveledLogger {
	return &LeveledLogger{
		level: DefaultLogLevel,
		debugLoggers: []DebugLogger{
			NewDebugLogger(os.Stdout),
		},
		infoLoggers: []InfoLogger{
			NewCommandLineInfoLogger(os.Stdout),
		},
		errorLoggers: []ErrorLogger{
			NewCommandLineErrorLogger(os.Stderr),
		},
	}
}

// NewStdoutLogger returns a logger configured
// to send all output to stdout (suitable for
// containerized/systemd operation).
func NewStdoutLogger(prefix string) *LeveledLogger {
	return NewCombinedLogger(prefix, os.Stdout)
}

// NewCombinedLogger returns a logger configured
// to send all output to the supplied io.Writer.
func NewCombinedLogger(prefix string, output io.Writer) *LeveledLogger {
	return &LeveledLogger{
		level: DefaultLogLevel,
		debugLoggers: []DebugLogger{
			NewDebugLogger(output),
		},
		infoLoggers: []InfoLogger{
			NewInfoLogger(prefix, output),
		},
		errorLoggers: []ErrorLogger{
			NewErrorLogger(prefix, output),
		},
	}
}

// NewTestLogger returns a logger and a *LogBuffer,
// with the logger configured to send all output into
// the buffer. The logger's level is set to DEBUG by default.
func NewTestLogger(prefix string) (*LeveledLogger, *LogBuffer) {
	var buf LogBuffer
	return NewCombinedLogger(prefix, &buf).
		WithLogLevel(LogLevelDebug), &buf
}
