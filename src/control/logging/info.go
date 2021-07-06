//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
