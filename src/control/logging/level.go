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
	"fmt"
	"strings"
	"sync/atomic"
)

const (
	// LogLevelDisabled disables any logging output
	LogLevelDisabled LogLevel = iota
	// LogLevelError emits messages at ERROR or higher
	LogLevelError
	// LogLevelInfo emits messages at INFO or higher
	LogLevelInfo
	// LogLevelDebug emits messages at DEBUG or higher
	LogLevelDebug

	strDisabled = "DISABLED"
	strError    = "ERROR"
	strInfo     = "INFO"
	strDebug    = "DEBUG"
)

// LogLevel represents the level at which the logger will emit log messages
type LogLevel int32

// Set safely sets the log level to the supplied level
func (ll *LogLevel) Set(newLevel LogLevel) {
	atomic.StoreInt32((*int32)(ll), int32(newLevel))
}

// Get returns the current log level
func (ll *LogLevel) Get() LogLevel {
	return LogLevel(atomic.LoadInt32((*int32)(ll)))
}

// SetString sets the log level from the supplied string.
func (ll *LogLevel) SetString(in string) error {
	var level LogLevel

	switch {
	case strings.EqualFold(in, strDisabled):
		level = LogLevelDisabled
	case strings.EqualFold(in, strError):
		level = LogLevelError
	case strings.EqualFold(in, strInfo):
		level = LogLevelInfo
	case strings.EqualFold(in, strDebug):
		level = LogLevelDebug
	default:
		return fmt.Errorf("%q is not a valid log level", in)
	}

	ll.Set(level)
	return nil
}

func (ll LogLevel) String() string {
	switch ll {
	case LogLevelDisabled:
		return strDisabled
	case LogLevelError:
		return strError
	case LogLevelInfo:
		return strInfo
	case LogLevelDebug:
		return strDebug
	default:
		return "UNKNOWN"
	}
}
