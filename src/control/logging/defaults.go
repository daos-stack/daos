//
// (C) Copyright 2019-2022 Intel Corporation.
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
// to send non-error output to stdout and error/debug
// output to stderr. The output format is suitable
// for command line utilities which don't want output
// to include timestamps and filenames.
func NewCommandLineLogger() *LeveledLogger {
	return &LeveledLogger{
		level: DefaultLogLevel,
		traceLoggers: []TraceLogger{
			NewTraceLogger(os.Stderr),
		},
		debugLoggers: []DebugLogger{
			NewDebugLogger(os.Stderr),
		},
		infoLoggers: []InfoLogger{
			NewCommandLineInfoLogger(os.Stdout),
		},
		noticeLoggers: []NoticeLogger{
			NewCommandLineNoticeLogger(os.Stderr),
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
		traceLoggers: []TraceLogger{
			NewTraceLogger(output),
		},
		debugLoggers: []DebugLogger{
			NewDebugLogger(output),
		},
		infoLoggers: []InfoLogger{
			NewInfoLogger(prefix, output),
		},
		noticeLoggers: []NoticeLogger{
			NewNoticeLogger(prefix, output),
		},
		errorLoggers: []ErrorLogger{
			NewErrorLogger(prefix, output),
		},
	}
}

// NewTestLogger returns a logger and a *LogBuffer,
// with the logger configured to send all output into
// the buffer. The logger's level is set to TRACE by default.
func NewTestLogger(prefix string) (*LeveledLogger, *LogBuffer) {
	var buf LogBuffer
	return NewCombinedLogger(prefix, &buf).
		WithLogLevel(LogLevelTrace), &buf
}

// NewTestCommandlineLogger returns a commandline logger and a *LogBuffer, with the logger
// configured to send all output into the buffer. The logger's level is set to TRACE by default.
func NewTestCommandLineLogger() (*LeveledLogger, *LogBuffer) {
	var buf LogBuffer
	return &LeveledLogger{
		level: LogLevelTrace,
		traceLoggers: []TraceLogger{
			NewTraceLogger(&buf),
		},
		debugLoggers: []DebugLogger{
			NewDebugLogger(&buf),
		},
		infoLoggers: []InfoLogger{
			NewCommandLineInfoLogger(&buf),
		},
		noticeLoggers: []NoticeLogger{
			NewCommandLineNoticeLogger(&buf),
		},
		errorLoggers: []ErrorLogger{
			NewCommandLineErrorLogger(&buf),
		},
	}, &buf
}
