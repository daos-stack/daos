//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package logging

import (
	"bytes"
	"io"
	"sync"
)

type (
	// Logger defines a standard logging interface
	Logger interface {
		EnabledFor(level LogLevel) bool
		TraceLogger
		Trace(msg string)
		DebugLogger
		Debug(msg string)
		InfoLogger
		Info(msg string)
		NoticeLogger
		Notice(msg string)
		ErrorLogger
		Error(msg string)
	}

	// TraceLogger defines an interface to be implemented
	// by Trace loggers.
	TraceLogger interface {
		Tracef(format string, args ...interface{})
	}

	// DebugLogger defines an interface to be implemented
	// by Debug loggers.
	DebugLogger interface {
		Debugf(format string, args ...interface{})
	}

	// InfoLogger defines an interface to be implemented
	// by Info loggers.
	InfoLogger interface {
		Infof(format string, args ...interface{})
	}

	// NoticeLogger defines an interface to be implemented
	// by Notice loggers.
	NoticeLogger interface {
		Noticef(format string, args ...interface{})
	}

	// ErrorLogger defines an interface to be implemented
	// by Error loggers.
	ErrorLogger interface {
		Errorf(format string, args ...interface{})
	}

	// Outputter defines an interface to be implemented
	// by output formatters.
	Outputter interface {
		Output(callDepth int, msg string) error
	}

	// LeveledLogger provides a logging implementation which
	// can emit log messages to multiple destinations with
	// different output formats.
	LeveledLogger struct {
		sync.RWMutex

		level         LogLevel
		traceLoggers  []TraceLogger
		debugLoggers  []DebugLogger
		infoLoggers   []InfoLogger
		noticeLoggers []NoticeLogger
		errorLoggers  []ErrorLogger
	}

	baseLogger struct {
		dest   io.Writer
		log    Outputter
		prefix string
	}
)

// SetLevel sets the logger's LogLevel, at or above
// which messages will be emitted.
func (ll *LeveledLogger) SetLevel(newLevel LogLevel) {
	ll.level.Set(newLevel)
}

// Level returns the logger's current LogLevel.
func (ll *LeveledLogger) Level() LogLevel {
	return ll.level.Get()
}

// EnabledFor returns true if the logger is enabled for the
// specified LogLevel.
func (ll *LeveledLogger) EnabledFor(level LogLevel) bool {
	return ll.level.Get() >= level
}

// ClearLevel clears all loggers for the specified level.
func (ll *LeveledLogger) ClearLevel(level LogLevel) {
	switch level {
	case LogLevelTrace:
		ll.traceLoggers = nil
	case LogLevelDebug:
		ll.debugLoggers = nil
	case LogLevelInfo:
		ll.infoLoggers = nil
	case LogLevelNotice:
		ll.noticeLoggers = nil
	case LogLevelError:
		ll.errorLoggers = nil
	default:
		ll.Errorf("unable to clear level %s", level)
	}
}

// WithLogLevel allows the logger's LogLevel to be set
// as part of a chained method call.
func (ll *LeveledLogger) WithLogLevel(level LogLevel) *LeveledLogger {
	ll.SetLevel(level)
	return ll
}

// WithTraceLogger adds the specified Trace logger to
// the logger as part of a chained method call.
func (ll *LeveledLogger) WithTraceLogger(newLogger TraceLogger) *LeveledLogger {
	ll.AddTraceLogger(newLogger)
	return ll
}

// Trace emits an unformatted message at Trace level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Trace(msg string) {
	ll.Tracef("%s", msg)
}

// Tracef emits a formatted message at Trace level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Tracef(format string, args ...interface{}) {
	if ll.Level() < LogLevelTrace {
		return
	}

	ll.RLock()
	loggers := ll.traceLoggers
	ll.RUnlock()

	for _, l := range loggers {
		l.Tracef(format, args...)
	}
}

// WithDebugLogger adds the specified Debug logger to
// the logger as part of a chained method call.
func (ll *LeveledLogger) WithDebugLogger(newLogger DebugLogger) *LeveledLogger {
	ll.AddDebugLogger(newLogger)
	return ll
}

// WithInfoLogger adds the specified Info logger to
// the logger as part of a chained method call.
func (ll *LeveledLogger) WithInfoLogger(newLogger InfoLogger) *LeveledLogger {
	ll.AddInfoLogger(newLogger)
	return ll
}

// WithNoticeLogger adds the specified Notice logger to
// the logger as part of a chained method call.
func (ll *LeveledLogger) WithNoticeLogger(newLogger NoticeLogger) *LeveledLogger {
	ll.AddNoticeLogger(newLogger)
	return ll
}

// WithErrorLogger adds the specified Error logger to
// the logger as part of a chained method call.
func (ll *LeveledLogger) WithErrorLogger(newLogger ErrorLogger) *LeveledLogger {
	ll.AddErrorLogger(newLogger)
	return ll
}

// AddTraceLogger adds the specified Trace logger to the logger.
func (ll *LeveledLogger) AddTraceLogger(newLogger TraceLogger) {
	ll.Lock()
	defer ll.Unlock()
	ll.traceLoggers = append(ll.traceLoggers, newLogger)
}

// AddDebugLogger adds the specified Debug logger to the logger.
func (ll *LeveledLogger) AddDebugLogger(newLogger DebugLogger) {
	ll.Lock()
	defer ll.Unlock()
	ll.debugLoggers = append(ll.debugLoggers, newLogger)
}

// Debug emits an unformatted message at Debug level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Debug(msg string) {
	ll.Debugf("%s", msg)
}

// Debugf emits a formatted message at Debug level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Debugf(format string, args ...interface{}) {
	if ll.Level() < LogLevelDebug {
		return
	}

	ll.RLock()
	loggers := ll.debugLoggers
	ll.RUnlock()

	for _, l := range loggers {
		l.Debugf(format, args...)
	}
}

// AddInfoLogger adds the specified Info logger to the logger.
func (ll *LeveledLogger) AddInfoLogger(newLogger InfoLogger) {
	ll.Lock()
	defer ll.Unlock()
	ll.infoLoggers = append(ll.infoLoggers, newLogger)
}

// Info emits an unformatted message at Info level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Info(msg string) {
	ll.Infof("%s", msg)
}

// Infof emits a formatted message at Info level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Infof(format string, args ...interface{}) {
	if ll.Level() < LogLevelInfo {
		return
	}

	ll.RLock()
	loggers := ll.infoLoggers
	ll.RUnlock()

	for _, l := range loggers {
		l.Infof(format, args...)
	}
}

// AddNoticeLogger adds the specified Notice logger to the logger.
func (ll *LeveledLogger) AddNoticeLogger(newLogger NoticeLogger) {
	ll.Lock()
	defer ll.Unlock()
	ll.noticeLoggers = append(ll.noticeLoggers, newLogger)
}

// Notice emits an unformatted message at Notice level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Notice(msg string) {
	ll.Noticef("%s", msg)
}

// Noticef emits a formatted message at Notice level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Noticef(format string, args ...interface{}) {
	if ll.Level() < LogLevelNotice {
		return
	}

	ll.RLock()
	loggers := ll.noticeLoggers
	ll.RUnlock()

	for _, l := range loggers {
		l.Noticef(format, args...)
	}
}

// AddErrorLogger adds the specified Error logger to the logger.
func (ll *LeveledLogger) AddErrorLogger(newLogger ErrorLogger) {
	ll.Lock()
	defer ll.Unlock()
	ll.errorLoggers = append(ll.errorLoggers, newLogger)
}

// Error emits an unformatted message at Error level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Error(msg string) {
	ll.Errorf("%s", msg)
}

// Errorf emits a formatted message at Error level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Errorf(format string, args ...interface{}) {
	if ll.Level() < LogLevelError {
		return
	}

	ll.RLock()
	loggers := ll.errorLoggers
	ll.RUnlock()

	for _, l := range loggers {
		l.Errorf(format, args...)
	}
}

// LogBuffer provides a thread-safe wrapper for bytes.Buffer.
// It only wraps a subset of bytes.Buffer's methods; just enough
// to implement io.Reader, io.Writer, and fmt.Stringer. The
// Reset() method is also wrapped in order to make it useful
// for testing.
type LogBuffer struct {
	sync.Mutex
	buf bytes.Buffer
}

func (lb *LogBuffer) Read(p []byte) (int, error) {
	lb.Lock()
	defer lb.Unlock()
	return lb.buf.Read(p)
}

func (lb *LogBuffer) Write(p []byte) (int, error) {
	lb.Lock()
	defer lb.Unlock()
	return lb.buf.Write(p)
}

func (lb *LogBuffer) String() string {
	lb.Lock()
	defer lb.Unlock()
	return lb.buf.String()
}

func (lb *LogBuffer) Reset() {
	lb.Lock()
	defer lb.Unlock()
	lb.buf.Reset()
}
