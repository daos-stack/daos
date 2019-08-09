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

const infoLogFlags = log.LstdFlags

// NewCommandLineInfoLogger returns an InfoLogger configured
// for outputting unadorned informational messages (i.e. no
// timestamps, source info, etc); typically used for CLI
// utility logging.
func NewCommandLineInfoLogger(output io.Writer) *DefaultInfoLogger {
	return &DefaultInfoLogger{
		baseLogger{
			dest: output,
			log:  log.New(output, "", emptyLogFlags),
		},
	}
}

// NewInfoLogger returns an InfoLogger configured for outputting
// informational messages with standard formatting (e.g. to stderr,
// logfile, etc.)
func NewInfoLogger(prefix string, output io.Writer) *DefaultInfoLogger {
	loggerPrefix := "INFO "
	if prefix != "" {
		loggerPrefix = prefix + " " + loggerPrefix
	}
	return &DefaultInfoLogger{
		baseLogger{
			dest:   output,
			prefix: prefix,
			log:    log.New(output, loggerPrefix, infoLogFlags),
		},
	}
}

// DefaultInfoLogger implements the InfoLogger interface.
type DefaultInfoLogger struct {
	baseLogger
}

// Infof emits a formatted informational message.
func (l *DefaultInfoLogger) Infof(format string, args ...interface{}) {
	out := fmt.Sprintf(format, args...)
	if err := l.log.Output(logOutputDepth, out); err != nil {
		fmt.Fprintf(os.Stderr, "logger Infof() failed: %s\n", err)
	}
}
