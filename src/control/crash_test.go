//
// (C) Copyright 2026 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control_test

import (
	"os"
	"os/signal"
	"runtime"
	"syscall"
	"testing"
)

func TestCrashWithCoreFile(t *testing.T) {
	if testing.Short() {
		t.Skip("skipping crash test in short mode")
	}

	// Enable core dumps by increasing RLIMIT_CORE
	var rlim syscall.Rlimit
	err := syscall.Getrlimit(syscall.RLIMIT_CORE, &rlim)
	if err != nil {
		t.Fatalf("failed to get rlimit: %v", err)
	}

	// Set core dump limit to unlimited (or a reasonable size)
	rlim.Cur = rlim.Max
	if rlim.Max == 0 {
		rlim.Max = 1024 * 1024 * 1024 // 1GB
		rlim.Cur = rlim.Max
	}

	err = syscall.Setrlimit(syscall.RLIMIT_CORE, &rlim)
	if err != nil {
		t.Logf("warning: failed to set rlimit for core dumps: %v", err)
		t.Logf("core dumps may not be generated. Try: ulimit -c unlimited")
	} else {
		t.Logf("core dump limit set to %d bytes", rlim.Cur)
	}

	// Intentionally trigger a crash by dereferencing a nil pointer
	t.Log("Triggering intentional crash to generate core file...")
	triggerSegmentationFault()
}

// triggerSegmentationFault deliberately causes a segmentation fault
// by dereferencing a nil pointer. This will cause the process to crash
// and generate a core file if core dumps are enabled.
func triggerSegmentationFault() {
	var nilPtr *int
	// This will cause a runtime panic which translates to a segmentation fault
	*nilPtr = 42
}

func TestPanicWithStackTrace(t *testing.T) {
	if testing.Short() {
		t.Skip("skipping panic test in short mode")
	}

	// Print diagnostic information
	t.Logf("GOARCH: %s", runtime.GOARCH)
	t.Logf("GOOS: %s", runtime.GOOS)
	t.Logf("NumGoroutine: %d", runtime.NumGoroutine())

	// Log instructions for enabling core dumps
	t.Log("\n=== Instructions for Core File Generation ===")
	t.Log("To enable core file generation on this system, run:")
	t.Log("  ulimit -c unlimited")
	t.Log("\nThen run this test:")
	t.Log("  go test -v -run TestCrashWithCoreFile")
	t.Log("\nCore files will be generated in:")
	t.Log("  /var/crash/ (system-wide, if apport is enabled)")
	t.Log("  ./ (current directory, if ulimit is set locally)")
	t.Log("==========================================\n")

	// This is a controlled panic that gets logged but caught by the test framework
	defer func() {
		if r := recover(); r != nil {
			t.Logf("Recovered from panic: %v", r)
		}
	}()

	panic("intentional panic to demonstrate crash handling")
}

func TestDirectSignalCrash(t *testing.T) {
	if testing.Short() {
		t.Skip("skipping signal crash test in short mode")
	}

	// Enable core dumps
	var rlim syscall.Rlimit
	syscall.Getrlimit(syscall.RLIMIT_CORE, &rlim)
	rlim.Cur = rlim.Max
	if rlim.Max == 0 {
		rlim.Max = 1024 * 1024 * 1024
		rlim.Cur = rlim.Max
	}
	syscall.Setrlimit(syscall.RLIMIT_CORE, &rlim)

	// Set up signal handling to log before crash
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGSEGV, syscall.SIGABRT)

	go func() {
		sig := <-sigChan
		t.Logf("Received signal: %v", sig)
		os.Exit(1)
	}()

	t.Log("Sending SIGSEGV to current process...")
	syscall.Kill(os.Getpid(), syscall.SIGSEGV)

	// If we reach here, something went wrong
	t.Fatal("signal was not handled as expected")
}
