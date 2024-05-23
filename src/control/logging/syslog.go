//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//go:build linux
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
	syslogNotice interface {
		WithSyslogOutput() NoticeLogger
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
	var noticeLoggers []NoticeLogger
	var errorLoggers []ErrorLogger

	for _, l := range ll.debugLoggers {
		if dl, ok := l.(syslogDebug); ok {
			debugLoggers = append(debugLoggers, dl.WithSyslogOutput())
		}
	}
	ll.debugLoggers = debugLoggers

	for _, l := range ll.infoLoggers {
		if il, ok := l.(syslogInfo); ok {
			infoLoggers = append(infoLoggers, il.WithSyslogOutput())
		}
	}
	ll.infoLoggers = infoLoggers

	for _, l := range ll.noticeLoggers {
		if nl, ok := l.(syslogNotice); ok {
			noticeLoggers = append(noticeLoggers, nl.WithSyslogOutput())
		}
	}
	ll.noticeLoggers = noticeLoggers

	for _, l := range ll.errorLoggers {
		if el, ok := l.(syslogError); ok {
			errorLoggers = append(errorLoggers, el.WithSyslogOutput())
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
func (l *DefaultNoticeLogger) WithSyslogOutput() NoticeLogger {
	// Disable timestamps -- they're supplied by syslog
	flags := noticeLogFlags ^ log.LstdFlags
	return &DefaultNoticeLogger{
		baseLogger{
			// Notices are condensed into the info level in syslog
			log: MustCreateSyslogger(syslog.LOG_INFO, flags),
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
