//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"fmt"
	"io"
	"log"
	"strings"

	"github.com/hashicorp/go-hclog"

	"github.com/daos-stack/daos/src/control/logging"
)

var (
	suppressedMessages = map[string]struct{}{
		"failed to contact": {},
	}
)

// hcLogger implements the hclog.Logger interface to
// provide a wrapper around our logger. As hclog is a
// structured (key/val) logger, we have to join the
// provided pairs into a single string before calling
// our logger.
type hcLogger struct {
	name        string
	impliedArgs []interface{}
	log         logging.Logger
}

func newHcLogger(l logging.Logger) *hcLogger {
	return &hcLogger{
		name: "DAOS hcLogger interface",
		log:  l,
	}
}

func (hcl *hcLogger) argString(args ...interface{}) string {
	allArgs := append(hcl.impliedArgs, args...)

	var argPairs []string
	for i := 0; i < len(allArgs); i += 2 {
		keyStr, ok := allArgs[i].(string)
		if !ok {
			continue
		}
		argPairs = append(argPairs, fmt.Sprintf("%s=%+v", keyStr, allArgs[i+1]))
	}
	return strings.Join(argPairs, " ")
}

func (hcl *hcLogger) Log(level hclog.Level, msg string, args ...interface{}) {
	switch level {
	case hclog.Trace:
		hcl.Trace(msg, args...)
	case hclog.Debug:
		hcl.Debug(msg, args...)
	case hclog.Info:
		hcl.Info(msg, args...)
	case hclog.Warn:
		hcl.Warn(msg, args...)
	case hclog.Error:
		hcl.Error(msg, args...)
	}
}

func (hcl *hcLogger) Trace(msg string, args ...interface{}) {}

func (hcl *hcLogger) Debug(msg string, args ...interface{}) {
	if _, found := suppressedMessages[msg]; found {
		return
	}
	hcl.log.Debugf(msg+": %s", hcl.argString(args...))
}

func (hcl *hcLogger) Info(msg string, args ...interface{}) {
	// We don't want raft stuff to be shown at INFO level.
	hcl.log.Debugf(msg+": %s", hcl.argString(args...))
}

func (hcl *hcLogger) Warn(msg string, args ...interface{}) {
	// Only errors should be printed at ERROR.
	hcl.log.Debugf(msg+": %s", hcl.argString(args...))
}

func (hcl *hcLogger) Error(msg string, args ...interface{}) {
	hcl.log.Errorf(msg+": %s", hcl.argString(args...))
}

func (hcl *hcLogger) IsTrace() bool { return false }

func (hcl *hcLogger) IsDebug() bool { return true }

func (hcl *hcLogger) IsInfo() bool { return true }

func (hcl *hcLogger) IsWarn() bool { return true }

func (hcl *hcLogger) IsError() bool { return true }

func (hcl *hcLogger) Name() string { return hcl.name }

func (hcl *hcLogger) ImpliedArgs() []interface{} { return hcl.impliedArgs }

func (hcl *hcLogger) With(args ...interface{}) hclog.Logger {
	out := newHcLogger(hcl.log)
	out.impliedArgs = args
	return out
}

func (hcl *hcLogger) Named(name string) hclog.Logger {
	out := newHcLogger(hcl.log)
	out.name = name
	return out
}

func (hcl *hcLogger) ResetNamed(name string) hclog.Logger {
	return hcl.Named(name)
}

func (hcl *hcLogger) SetLevel(level hclog.Level) {}

func (hcl *hcLogger) StandardLogger(opts *hclog.StandardLoggerOptions) *log.Logger {
	return log.New(hcl.StandardWriter(opts), "", 0)
}

type writerFunc func(p []byte) (int, error)

func (w writerFunc) Write(p []byte) (int, error) {
	return w(p)
}

func (hcl *hcLogger) StandardWriter(opts *hclog.StandardLoggerOptions) io.Writer {
	return writerFunc(func(p []byte) (int, error) {
		hcl.log.Errorf("%s", string(p))
		return len(p), nil
	})
}
