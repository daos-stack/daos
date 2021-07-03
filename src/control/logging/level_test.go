//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
