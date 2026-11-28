//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"path/filepath"
	"runtime/cgo"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestControlC_InitFini(t *testing.T) {
	for name, tc := range map[string]struct {
		configFile string
		logFile    string
		logLevel   string
		useTempLog bool
		expRC      int
	}{
		"defaults (insecure localhost)": {},
		"with logging": {
			useTempLog: true,
			logLevel:   "debug",
		},
		"nonexistent config file": {
			configFile: "/nonexistent/config/file.yml",
			expRC:      int(daos.Nonexistent),
		},
		"non-writable log path": {
			logFile: "/nonexistent/dir/test.log",
			expRC:   int(daos.Nonexistent),
		},
	} {
		t.Run(name, func(t *testing.T) {
			logFile := tc.logFile
			if tc.useTempLog {
				logFile = filepath.Join(t.TempDir(), "test.log")
			}

			handle, rc := callInit(tc.configFile, logFile, tc.logLevel)
			if rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
			if tc.expRC != 0 {
				if handle != 0 {
					callFini(handle)
					t.Fatal("expected zero handle on init failure")
				}
				return
			}
			if handle == 0 {
				t.Fatal("expected non-zero handle")
			}
			callFini(handle)

			if tc.useTempLog {
				if _, err := os.Stat(logFile); err != nil {
					t.Fatalf("expected log file to be created: %s", err)
				}
			}
		})
	}
}

func TestControlC_InitNilHandleOut(t *testing.T) {
	if rc := callInitNilHandleOut(); rc != int(daos.InvalidInput) {
		t.Fatalf("rc=%d, want %d (InvalidInput)", rc, int(daos.InvalidInput))
	}
}

func TestControlC_FiniZeroHandle(t *testing.T) {
	// Should not panic with zero handle
	callFini(0)
}

func TestControlC_BogusHandlePanicRecovery(t *testing.T) {
	// Any non-zero integer that was never handed out by cgo.NewHandle will
	// cause cgo.Handle.Value() to panic; getContext recovers it locally and
	// reports an invalid handle rather than a generic export panic.
	const bogus = cgo.Handle(0xdeadbeef)

	if rc := callCheckSwitch(bogus, true); rc != int(daos.InvalidInput) {
		t.Fatalf("rc=%d, want %d (InvalidInput)", rc, int(daos.InvalidInput))
	}
}
