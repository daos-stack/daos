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
	"fmt"
	"io"
	"log"
	"os"
)

const errorLogFlags = log.LstdFlags

// NewCommandLineErrorLogger returns an ErrorLogger configured
// for outputting unadorned error messages (i.e. no timestamps,
// source info, etc); typically used for CLI utility logging.
func NewCommandLineErrorLogger(output io.Writer) *DefaultErrorLogger {
	return &DefaultErrorLogger{
		baseLogger{
			dest: output,
			log:  log.New(output, "ERROR: ", emptyLogFlags),
		},
	}
}

// NewErrorLogger returns an ErrorLogger configured for outputting
// error messages with standard formatting (e.g. to stderr, logfile, etc.)
func NewErrorLogger(prefix string, output io.Writer) *DefaultErrorLogger {
	loggerPrefix := "ERROR "
	if prefix != "" {
		loggerPrefix = prefix + " " + loggerPrefix
	}
	return &DefaultErrorLogger{
		baseLogger{
			dest:   output,
			prefix: prefix,
			log:    log.New(output, loggerPrefix, errorLogFlags),
		},
	}
}

// DefaultErrorLogger implements the ErrorLogger interface.
type DefaultErrorLogger struct {
	baseLogger
}

// Errorf emits a formatted error message.
func (l *DefaultErrorLogger) Errorf(format string, args ...interface{}) {
	out := fmt.Sprintf(format, args...)
	if err := l.log.Output(logOutputDepth, out); err != nil {
		fmt.Fprintf(os.Stderr, "logger Errorf() failed: %s\n", err)
	}
}
