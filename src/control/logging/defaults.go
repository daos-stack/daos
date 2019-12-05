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
