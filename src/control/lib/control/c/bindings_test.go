//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestInitFini(t *testing.T) {
	// Test with default config (nil args uses insecure localhost)
	handle, rc := callInit("", "", "")
	if rc != 0 {
		t.Fatalf("expected RC 0, got %d", rc)
	}
	if handle == 0 {
		t.Fatal("expected non-zero handle")
	}

	// Clean up
	callFini(handle)
}

func TestInitWithLogging(t *testing.T) {
	// Create temp log file
	tmpDir := t.TempDir()
	logFile := filepath.Join(tmpDir, "test.log")

	handle, rc := callInit("", logFile, "debug")
	if rc != 0 {
		t.Fatalf("expected RC 0, got %d", rc)
	}
	if handle == 0 {
		t.Fatal("expected non-zero handle")
	}

	// Clean up
	callFini(handle)

	// Verify log file was created
	if _, err := os.Stat(logFile); os.IsNotExist(err) {
		t.Fatal("expected log file to be created")
	}
}

func TestInitInvalidConfig(t *testing.T) {
	// Test with non-existent config file
	handle, rc := callInit("/nonexistent/config/file.yml", "", "")
	if rc == 0 {
		callFini(handle)
		t.Fatal("expected error for non-existent config file, got success")
	}
}

func TestInitInvalidLogPath(t *testing.T) {
	// Test with non-writable log path
	handle, rc := callInit("", "/nonexistent/dir/test.log", "")
	if rc == 0 {
		callFini(handle)
		t.Fatal("expected error for non-writable log path, got success")
	}
}

func TestInitNilHandleOut(t *testing.T) {
	rc := callInitNilHandleOut()
	if rc == 0 {
		t.Fatal("expected error for nil handleOut, got success")
	}
}

func TestFiniZeroHandle(t *testing.T) {
	// Should not panic with zero handle
	callFini(0)
}
