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

package log

import (
	"fmt"
	"io"
	"log"
)

// Log levels.
const (
	Error = iota
	Debug
)

// global default logger
var logger *Logger

// NewLogger creates a Logger instance and returns reference
//
// level represents the minimum logging level to be written,
// name is prefixed to any log entry and writer the target
// io.Writer interface to be written to.
func NewLogger(level int, name string, writer io.Writer) *Logger {
	var l Logger
	l.logger = log.New(writer, name, log.LstdFlags|log.Lshortfile)
	l.level = level
	return &l
}

// NewDefaultLogger instantiates default Logger
func NewDefaultLogger(level int, name string, writer io.Writer) {
	logger = NewLogger(level, name, writer)
}

// Errorf logs an error message to the default logger
func Errorf(format string, v ...interface{}) {
	logger.Errordf(3, format, v...)
}

// Errordf logs a debug message to the default logger at given call depth
func Errordf(calldepth int, format string, v ...interface{}) {
	logger.Errordf(calldepth, format, v...)
}

// Debugf logs a debug message to the default logger
func Debugf(format string, v ...interface{}) {
	logger.Debugdf(3, format, v...)
}

// Debugdf logs a debug message to the default logger at given call depth
func Debugdf(calldepth int, format string, v ...interface{}) {
	logger.Debugdf(calldepth, format, v...)
}

// SetLevel sets the log mask for the default logger
func SetLevel(level int) {
	logger.level = level
}

// SetOutput sets the output destination for the default logger
func SetOutput(w io.Writer) {
	logger.logger.SetOutput(w)
}

// Logger struct contains reference and level
type Logger struct {
	logger *log.Logger
	level  int
}

// Errorf logs an error message
func (l *Logger) Errorf(format string, v ...interface{}) {
	if l.level >= Error {
		l.logger.Output(2, fmt.Sprintf("error: "+format, v...))
	}
}

// Errordf logs an error messagae, calldepth is the count of the number of
// frames to skip when computing the file name and line number
func (l *Logger) Errordf(calldepth int, format string, v ...interface{}) {
	if l.level >= Error {
		l.logger.Output(calldepth, fmt.Sprintf("error: "+format, v...))
	}
}

// Debugf logs a debug message
func (l *Logger) Debugf(format string, v ...interface{}) {
	if l.level >= Debug {
		l.logger.Output(2, fmt.Sprintf("debug: "+format, v...))
	}
}

// Debugdf logs a debug messagae, calldepth is the count of the number of
// frames to skip when computing the file name and line number
func (l *Logger) Debugdf(calldepth int, format string, v ...interface{}) {
	if l.level >= Debug {
		l.logger.Output(calldepth, fmt.Sprintf("debug: "+format, v...))
	}
}
