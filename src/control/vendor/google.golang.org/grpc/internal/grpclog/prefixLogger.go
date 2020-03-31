/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

package grpclog

// PrefixLogger does logging with a prefix.
//
// Logging method on a nil logs without any prefix.
type PrefixLogger struct {
	prefix string
}

// Infof does info logging.
func (pl *PrefixLogger) Infof(format string, args ...interface{}) {
	if pl != nil {
		// Handle nil, so the tests can pass in a nil logger.
		format = pl.prefix + format
	}
	Logger.Infof(format, args...)
}

// Warningf does warning logging.
func (pl *PrefixLogger) Warningf(format string, args ...interface{}) {
	if pl != nil {
		format = pl.prefix + format
	}
	Logger.Warningf(format, args...)
}

// Errorf does error logging.
func (pl *PrefixLogger) Errorf(format string, args ...interface{}) {
	if pl != nil {
		format = pl.prefix + format
	}
	Logger.Errorf(format, args...)
}

// Debugf does info logging at verbose level 2.
func (pl *PrefixLogger) Debugf(format string, args ...interface{}) {
	if Logger.V(2) {
		pl.Infof(format, args...)
	}
}

// NewPrefixLogger creates a prefix logger with the given prefix.
func NewPrefixLogger(prefix string) *PrefixLogger {
	return &PrefixLogger{prefix: prefix}
}
