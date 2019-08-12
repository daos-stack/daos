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
