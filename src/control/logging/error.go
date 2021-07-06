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
