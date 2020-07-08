//
// (C) Copyright 2020 Intel Corporation.
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

package system

import (
	"fmt"
	"io"
	"log"
	"strings"

	"github.com/hashicorp/go-hclog"

	"github.com/daos-stack/daos/src/control/logging"
)

// hcLogCompat implements the hclog.Logger interface to
// provide a wrapper around our logger. As hclog is a
// structured (key/val) logger, we have to join the
// provided pairs into a single string before calling
// our logger.
type hcLogCompat struct {
	log logging.Logger
}

func newCompatLogger(l logging.Logger) *hcLogCompat {
	return &hcLogCompat{log: l}
}

func (hlc *hcLogCompat) argString(args ...interface{}) string {
	var argPairs []string
	for i := 0; i < len(args); i += 2 {
		fmtStr, ok := args[i].(string)
		if !ok {
			continue
		}
		argPairs = append(argPairs, fmt.Sprintf(fmtStr+"=%+v", args[i+1]))
	}
	return strings.Join(argPairs, " ")
}

func (hlc *hcLogCompat) Trace(msg string, args ...interface{}) {}

func (hlc *hcLogCompat) Debug(msg string, args ...interface{}) {
	hlc.log.Debugf(msg+": %s", hlc.argString(args...))
}

func (hlc *hcLogCompat) Info(msg string, args ...interface{}) {
	// We don't want raft stuff to be shown at INFO level.
	hlc.log.Debugf(msg+": %s", hlc.argString(args...))
}

func (hlc *hcLogCompat) Warn(msg string, args ...interface{}) {
	// As we don't have a warn level with our logger, just
	// escalate these messages to ERROR.
	hlc.log.Errorf(msg+": %s", hlc.argString(args...))
}

func (hlc *hcLogCompat) Error(msg string, args ...interface{}) {
	hlc.log.Errorf(msg+": %s", hlc.argString(args...))
}

func (hlc *hcLogCompat) IsTrace() bool { return false }

func (hlc *hcLogCompat) IsDebug() bool { return true }

func (hlc *hcLogCompat) IsInfo() bool { return true }

func (hlc *hcLogCompat) IsWarn() bool { return true }

func (hlc *hcLogCompat) IsError() bool { return true }

func (hlc *hcLogCompat) With(args ...interface{}) hclog.Logger { panic("not supported") }

func (hlc *hcLogCompat) Named(name string) hclog.Logger { panic("not supported") }

func (hlc *hcLogCompat) ResetNamed(name string) hclog.Logger { panic("not supported") }

func (hlc *hcLogCompat) SetLevel(level hclog.Level) { panic("not supported") }

func (hlc *hcLogCompat) StandardLogger(opts *hclog.StandardLoggerOptions) *log.Logger {
	panic("not supported")
}

func (hlc *hcLogCompat) StandardWriter(opts *hclog.StandardLoggerOptions) io.Writer {
	panic("not supported")
}
