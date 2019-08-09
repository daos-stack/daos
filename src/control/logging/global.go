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

// TODO(mjmac): This file should eventually be removed, as the
// use of a package-level logger is an antipattern (1). It's
// provided for the moment in order to make incremental steps
// away from this model.
//
// NB: The functionality provided here is deliberately minimal.
// If more control is desired, use a non-global logger instance
// which is provided to users via dependency injection.
//
// 1. https://dave.cheney.net/2017/01/23/the-package-level-logger-anti-pattern

// default to setting things up for CLI logging
var globalLogger = NewCommandLineLogger()

// SetLogger replaces the package-level logger.
//
// NB: Because this is a convenience function, we have
// to choose between making the caller remember to update
// the loglevel after setting the global logger, or else
// setting the new logger's level based on the previous
// one. Setting it based on the previous logger seems
// like the least surprising behavior.
func SetLogger(newLogger *LeveledLogger) {
	level := DefaultLogLevel
	if globalLogger != nil {
		level = globalLogger.level
	}
	newLogger.SetLevel(level)

	globalLogger = newLogger
}

// SetJSONOutput switches all logging output
// to JSON format.
func SetJSONOutput() {
	globalLogger = globalLogger.WithJSONOutput()
}

// SetLevel sets the loglevel for the package-level logger.
func SetLevel(newLevel LogLevel) {
	globalLogger.SetLevel(newLevel)
}

// Debug emits a message at DEBUG level.
func Debug(msg string) {
	globalLogger.Debugf(msg)
}

// Debugf emits a formatted argument list at DEBUG level.
func Debugf(format string, args ...interface{}) {
	globalLogger.Debugf(format, args...)
}

// Info emits a message at INFO level.
func Info(msg string) {
	globalLogger.Infof(msg)
}

// Infof emits a formatted argument list at INFO level.
func Infof(format string, args ...interface{}) {
	globalLogger.Infof(format, args...)
}

// Error emits a message at ERROR level.
func Error(msg string) {
	globalLogger.Errorf(msg)
}

// Errorf emits a formatted argument list at ERROR level.
func Errorf(format string, args ...interface{}) {
	globalLogger.Errorf(format, args...)
}
