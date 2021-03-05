//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build linux

package logging

import (
	"log"
	"log/syslog"
)

// MustCreateSyslogger attempts to create a *log.Logger configured
// for output to the system log daemon. If it fails, it will panic.
func MustCreateSyslogger(prio syslog.Priority, flags int) *log.Logger {
	logger, err := syslog.NewLogger(prio, flags)
	if err != nil {
		panic(err)
	}
	return logger
}

type (
	syslogDebug interface {
		WithSyslogOutput() DebugLogger
	}
	syslogInfo interface {
		WithSyslogOutput() InfoLogger
	}
	syslogError interface {
		WithSyslogOutput() ErrorLogger
	}
)

// WithSyslogOutput is a convenience method to set all
// logging outputs to the Syslog formatter.
func (ll *LeveledLogger) WithSyslogOutput() *LeveledLogger {
	ll.Lock()
	defer ll.Unlock()

	var debugLoggers []DebugLogger
	var infoLoggers []InfoLogger
	var errorLoggers []ErrorLogger

	for _, l := range ll.debugLoggers {
		if syslogger, ok := l.(syslogDebug); ok {
			if dl, ok := syslogger.WithSyslogOutput().(DebugLogger); ok {
				debugLoggers = append(debugLoggers, dl)
			}
		}
	}
	ll.debugLoggers = debugLoggers

	for _, l := range ll.infoLoggers {
		if syslogger, ok := l.(syslogInfo); ok {
			if il, ok := syslogger.WithSyslogOutput().(InfoLogger); ok {
				infoLoggers = append(infoLoggers, il)
			}
		}
	}
	ll.infoLoggers = infoLoggers

	for _, l := range ll.errorLoggers {
		if syslogger, ok := l.(syslogError); ok {
			if el, ok := syslogger.WithSyslogOutput().(ErrorLogger); ok {
				errorLoggers = append(errorLoggers, el)
			}
		}
	}
	ll.errorLoggers = errorLoggers

	return ll
}

// WithSyslogOutput switches the logger's output to emit messages
// via the system logging service.
func (l *DefaultErrorLogger) WithSyslogOutput() ErrorLogger {
	// Disable timestamps -- they're supplied by syslog
	flags := errorLogFlags ^ log.LstdFlags
	return &DefaultErrorLogger{
		baseLogger{
			log: MustCreateSyslogger(syslog.LOG_ERR, flags),
		},
	}
}

// WithSyslogOutput switches the logger's output to emit messages
// via the system logging service.
func (l *DefaultInfoLogger) WithSyslogOutput() InfoLogger {
	// Disable timestamps -- they're supplied by syslog
	flags := infoLogFlags ^ log.LstdFlags
	return &DefaultInfoLogger{
		baseLogger{
			log: MustCreateSyslogger(syslog.LOG_INFO, flags),
		},
	}
}

// WithSyslogOutput switches the logger's output to emit messages
// via the system logging service.
func (l *DefaultDebugLogger) WithSyslogOutput() DebugLogger {
	// NB: Leave the timestamp alone here, because we want
	// the microsecond resolution.
	return &DefaultDebugLogger{
		baseLogger{
			log: MustCreateSyslogger(syslog.LOG_DEBUG, debugLogFlags),
		},
	}
}
