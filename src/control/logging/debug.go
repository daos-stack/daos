//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package logging

import (
	"fmt"
	"io"
	"log"
	"os"
	"runtime"
	"strings"

	"github.com/google/uuid"
)

var (
	// knownWrappers provides a lookup table of known
	// function wrapper names to be ignored when determining
	// the real caller location.
	knownWrappers = map[string]struct{}{
		"Trace":   {},
		"Tracef":  {},
		"Debug":   {},
		"Debugf":  {},
		"Info":    {},
		"Infof":   {},
		"Notice":  {},
		"Noticef": {},
		"Warn":    {},
		"Warnf":   {},
		"Error":   {},
		"Errorf":  {},
	}
)

const debugLogFlags = log.Ldate | log.Lmicroseconds | log.Lshortfile

// DefaultTraceLogger implements the TraceLogger interface by
// wrapping the DefaultDebugLogger implementation.
type DefaultTraceLogger DefaultDebugLogger

// Tracef emits a formatted trace message.
func (l *DefaultTraceLogger) Tracef(format string, args ...interface{}) {
	(*DefaultDebugLogger)(l).Debugf(format, args...)
}

// NewTraceLogger returns a DebugLogger configured for outputting
// trace-level messages.
func NewTraceLogger(dest io.Writer) *DefaultTraceLogger {
	return &DefaultTraceLogger{
		baseLogger{
			dest: dest,
			log:  log.New(dest, "TRACE ", debugLogFlags),
		},
	}
}

// NewDebugLogger returns a DebugLogger configured for outputting
// debugging messages.
func NewDebugLogger(dest io.Writer) *DefaultDebugLogger {
	return &DefaultDebugLogger{
		baseLogger{
			dest: dest,
			log:  log.New(dest, "DEBUG ", debugLogFlags),
		},
	}
}

// DefaultDebugLogger implements the DebugLogger interface.
type DefaultDebugLogger struct {
	baseLogger
}

// Debugf emits a formatted debug message.
func (l *DefaultDebugLogger) Debugf(format string, args ...interface{}) {
	depth := logOutputDepth

	// Adjust depth to account for any convenience wrappers. Enables
	// printing of correct caller info.
	pc := make([]uintptr, depth+5)
	n := runtime.Callers(depth, pc)
	if n > 0 {
		pc = pc[:n]
		frames := runtime.CallersFrames(pc)
		for {
			frame, more := frames.Next()
			if !more {
				break
			}
			fnName := frame.Function[strings.LastIndex(frame.Function, ".")+1:]
			if _, found := knownWrappers[fnName]; found {
				depth++
			}
		}
	}

	out := fmt.Sprintf(format, args...)
	if err := l.log.Output(depth, out); err != nil {
		fmt.Fprintf(os.Stderr, "logger Debugf() failed: %s\n", err)
	}
}

// ShortUUID returns a truncated UUID string suitable for debug logging.
func ShortUUID(u uuid.UUID) string {
	return u.String()[:8]
}
