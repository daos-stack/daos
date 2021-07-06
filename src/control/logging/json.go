//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package logging

import (
	"encoding/json"
	"io"
	"log"
	"runtime"
	"time"
)

const (
	// Use ISO8601 format for timestamps as it's
	// widely supported by parsers (e.g. javascript, etc).
	iso8601NoMicro = "2006-01-02T15:04:05Z0700"
	iso8601        = "2006-01-02T15:04:05.000000Z0700"
)

type (
	// JSONFormatter emits JSON-formatted log output
	JSONFormatter struct {
		output io.Writer
		level  string
		extra  string
		flags  int
	}

	logStruct struct {
		Level   string `json:"level"`
		Time    string `json:"time"`
		Extra   string `json:"extra,omitempty"`
		Source  string `json:"source,omitempty"`
		Message string `json:"message"`
	}
)

func formatJSONTime(t time.Time, flags int) string {
	if flags&log.LUTC != 0 {
		t = t.UTC()
	}

	if flags&log.Lmicroseconds != 0 {
		return t.Format(iso8601)
	}
	return t.Format(iso8601NoMicro)
}

// Output emulates log.Logger's Output(), but formats
// the message as a JSON-structured log entry.
func (f *JSONFormatter) Output(callDepth int, msg string) error {
	now := time.Now()
	var file string
	var line int

	if f.flags&(log.Lshortfile|log.Llongfile) != 0 {
		var ok bool
		_, file, line, ok = runtime.Caller(callDepth)
		if !ok {
			file = "???"
			line = 0
		}
	}

	buf, err := json.Marshal(logStruct{
		Time:    formatJSONTime(now, f.flags),
		Level:   f.level,
		Extra:   f.extra,
		Source:  formatSource(file, line, f.flags),
		Message: msg,
	})
	if err != nil {
		return err
	}

	if _, err := f.output.Write(buf); err != nil {
		return err
	}
	_, err = f.output.Write([]byte("\n"))
	return err
}

// NewJSONFormatter returns a *JSONFormatter configured to
// emit JSON-formatted output.
func NewJSONFormatter(output io.Writer, level, extraData string, flags int) *JSONFormatter {
	return &JSONFormatter{
		output: output,
		level:  level,
		extra:  extraData,
		flags:  flags,
	}
}

type (
	jsonDebug interface {
		WithJSONOutput() DebugLogger
	}
	jsonInfo interface {
		WithJSONOutput() InfoLogger
	}
	jsonError interface {
		WithJSONOutput() ErrorLogger
	}
)

// WithJSONOutput is a convenience method to set all
// logging outputs to the JSON formatter.
func (ll *LeveledLogger) WithJSONOutput() *LeveledLogger {
	ll.Lock()
	defer ll.Unlock()

	var debugLoggers []DebugLogger
	var infoLoggers []InfoLogger
	var errorLoggers []ErrorLogger

	for _, l := range ll.debugLoggers {
		if jsonLogger, ok := l.(jsonDebug); ok {
			if dl, ok := jsonLogger.WithJSONOutput().(DebugLogger); ok {
				debugLoggers = append(debugLoggers, dl)
			}
		}
	}
	ll.debugLoggers = debugLoggers

	for _, l := range ll.infoLoggers {
		if jsonLogger, ok := l.(jsonInfo); ok {
			if il, ok := jsonLogger.WithJSONOutput().(InfoLogger); ok {
				infoLoggers = append(infoLoggers, il)
			}
		}
	}
	ll.infoLoggers = infoLoggers

	for _, l := range ll.errorLoggers {
		if jsonLogger, ok := l.(jsonError); ok {
			if el, ok := jsonLogger.WithJSONOutput().(ErrorLogger); ok {
				errorLoggers = append(errorLoggers, el)
			}
		}
	}
	ll.errorLoggers = errorLoggers

	return ll
}

// WithJSONOutput switches the logger's output to use structured
// JSON formatting.
func (l *DefaultErrorLogger) WithJSONOutput() ErrorLogger {
	return &DefaultErrorLogger{
		baseLogger{
			dest:   l.dest,
			prefix: l.prefix,
			log:    NewJSONFormatter(l.dest, "ERROR", l.prefix, errorLogFlags),
		},
	}
}

// WithJSONOutput switches the logger's output to use structured
// JSON formatting.
func (l *DefaultInfoLogger) WithJSONOutput() InfoLogger {
	return &DefaultInfoLogger{
		baseLogger{
			dest:   l.dest,
			prefix: l.prefix,
			log:    NewJSONFormatter(l.dest, "INFO", l.prefix, infoLogFlags),
		},
	}
}

// WithJSONOutput switches the logger's output to use structured
// JSON formatting.
func (l *DefaultDebugLogger) WithJSONOutput() DebugLogger {
	return &DefaultDebugLogger{
		baseLogger{
			dest: l.dest,
			log:  NewJSONFormatter(l.dest, "DEBUG", "", debugLogFlags),
		},
	}
}
