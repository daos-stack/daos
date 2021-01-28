//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

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
	log logging.Logger
}

func newHcLogger(l logging.Logger) *hcLogger {
	return &hcLogger{log: l}
}

func (hlc *hcLogger) argString(args ...interface{}) string {
	var argPairs []string
	for i := 0; i < len(args); i += 2 {
		keyStr, ok := args[i].(string)
		if !ok {
			continue
		}
		argPairs = append(argPairs, fmt.Sprintf("%s=%+v", keyStr, args[i+1]))
	}
	return strings.Join(argPairs, " ")
}

func (hlc *hcLogger) Trace(msg string, args ...interface{}) {}

func (hlc *hcLogger) Debug(msg string, args ...interface{}) {
	if _, found := suppressedMessages[msg]; found {
		return
	}
	hlc.log.Debugf(msg+": %s", hlc.argString(args...))
}

func (hlc *hcLogger) Info(msg string, args ...interface{}) {
	// We don't want raft stuff to be shown at INFO level.
	hlc.log.Debugf(msg+": %s", hlc.argString(args...))
}

func (hlc *hcLogger) Warn(msg string, args ...interface{}) {
	// Only errors should be printed at ERROR.
	hlc.log.Debugf(msg+": %s", hlc.argString(args...))
}

func (hlc *hcLogger) Error(msg string, args ...interface{}) {
	hlc.log.Errorf(msg+": %s", hlc.argString(args...))
}

func (hlc *hcLogger) IsTrace() bool { return false }

func (hlc *hcLogger) IsDebug() bool { return true }

func (hlc *hcLogger) IsInfo() bool { return true }

func (hlc *hcLogger) IsWarn() bool { return true }

func (hlc *hcLogger) IsError() bool { return true }

func (hlc *hcLogger) With(args ...interface{}) hclog.Logger { panic("not supported") }

func (hlc *hcLogger) Named(name string) hclog.Logger { panic("not supported") }

func (hlc *hcLogger) ResetNamed(name string) hclog.Logger { panic("not supported") }

func (hlc *hcLogger) SetLevel(level hclog.Level) { panic("not supported") }

func (hlc *hcLogger) StandardLogger(opts *hclog.StandardLoggerOptions) *log.Logger {
	panic("not supported")
}

func (hlc *hcLogger) StandardWriter(opts *hclog.StandardLoggerOptions) io.Writer {
	panic("not supported")
}
