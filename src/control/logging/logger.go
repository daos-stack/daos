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
	"sync"
)

type (
	// Logger defines a standard logging interface
	Logger interface {
		DebugLogger
		Debug(msg string)
		InfoLogger
		Info(msg string)
		ErrorLogger
		Error(msg string)
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

		level        LogLevel
		debugLoggers []DebugLogger
		infoLoggers  []InfoLogger
		errorLoggers []ErrorLogger
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

// WithLogLevel allows the logger's LogLevel to be set
// as part of a chained method call.
func (ll *LeveledLogger) WithLogLevel(level LogLevel) *LeveledLogger {
	ll.SetLevel(level)
	return ll
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

// WithErrorLogger adds the specified Error logger to
// the logger as part of a chained method call.
func (ll *LeveledLogger) WithErrorLogger(newLogger ErrorLogger) *LeveledLogger {
	ll.AddErrorLogger(newLogger)
	return ll
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
	ll.Debugf(msg)
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
	ll.Infof(msg)
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

// AddErrorLogger adds the specified Error logger to the logger.
func (ll *LeveledLogger) AddErrorLogger(newLogger ErrorLogger) {
	ll.Lock()
	defer ll.Unlock()
	ll.errorLoggers = append(ll.errorLoggers, newLogger)
}

// Error emits an unformatted message at Error level, if
// the logger is configured to do so.
func (ll *LeveledLogger) Error(msg string) {
	ll.Errorf(msg)
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
