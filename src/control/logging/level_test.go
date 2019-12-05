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
package logging_test

import (
	"testing"

	"github.com/daos-stack/daos/src/control/logging"
)

func TestLogLevelToString(t *testing.T) {
	for name, tc := range map[string]struct {
		expected string
		level    logging.LogLevel
	}{
		"Zero Value": {expected: "DISABLED"},
		"Disabled":   {expected: "DISABLED", level: logging.LogLevelDisabled},
		"Error":      {expected: "ERROR", level: logging.LogLevelError},
		"Info":       {expected: "INFO", level: logging.LogLevelInfo},
		"Debug":      {expected: "DEBUG", level: logging.LogLevelDebug},
		"Unknown":    {expected: "UNKNOWN", level: logging.LogLevel(42)},
	} {
		t.Run(name, func(t *testing.T) {
			got := tc.level.String()
			if got != tc.expected {
				t.Fatalf("expected %q, got %q", tc.expected, got)
			}
		})
	}
}

func TestLogLevelFromString(t *testing.T) {
	for name, tc := range map[string]struct {
		expected  logging.LogLevel
		shouldErr bool
	}{
		"":          {shouldErr: true},
		"Bad Level": {shouldErr: true},
		"Disabled":  {expected: logging.LogLevelDisabled},
		"disabled":  {expected: logging.LogLevelDisabled},
		"Error":     {expected: logging.LogLevelError},
		"error":     {expected: logging.LogLevelError},
		"Info":      {expected: logging.LogLevelInfo},
		"info":      {expected: logging.LogLevelInfo},
		"Debug":     {expected: logging.LogLevelDebug},
		"debug":     {expected: logging.LogLevelDebug},
	} {
		t.Run(name, func(t *testing.T) {
			var level logging.LogLevel
			err := level.SetString(name)
			if tc.shouldErr {
				if err == nil {
					t.Fatal("expected error wasn't returned")
				}
				return
			}
			if level != tc.expected {
				t.Fatalf("expected %s to be %s", level, tc.expected)
			}
		})
	}
}
