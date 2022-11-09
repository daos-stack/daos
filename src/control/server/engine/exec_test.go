//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	testModeVar = "engine_TEST_MODE"
	testSep     = "==="
	testArgsStr = "engine_TEST_ARGS"
	testEnvStr  = "engine_TEST_ENV"
)

func TestMain(m *testing.M) {
	switch os.Getenv(testModeVar) {
	case "":
		os.Exit(m.Run())
	case "RunnerNormalExit":
		// remove this once we're running so that it doesn't pollute test results
		os.Unsetenv("LD_LIBRARY_PATH")
		os.Unsetenv(testModeVar)
		env := os.Environ()
		sort.Strings(env)
		fmt.Printf("%s%s%s\n", testEnvStr, testSep, strings.Join(env, " "))
		fmt.Printf("%s%s%s\n", testArgsStr, testSep, strings.Join(os.Args[1:], " "))
		os.Exit(0)
	case "RunnerContextExit":
		time.Sleep(30 * time.Second)
		os.Exit(1)
	}
}

// createFakeBinary writes out a copy of this test file to
// an executable file in the test directory. This fake binary
// is named the same as a DAOS I/O Engine binary so that it's
// located via findBinary(). When it is invoked by Runner.run(),
// the TestMain() function is used to do some simple simulation
// of the real binary's behavior.
func createFakeBinary(t *testing.T) {
	t.Helper()

	// stash this to restore after scrubbing the rest of the env
	ld_library_path := os.Getenv("LD_LIBRARY_PATH")
	// ensure that we have a clean environment for testing
	os.Clearenv()
	os.Setenv("LD_LIBRARY_PATH", ld_library_path)

	testDir := filepath.Dir(os.Args[0])
	fakeBin := filepath.Join(testDir, engineBin)

	// Don't regenerate the test binary if it already exists.
	// The package tests are run sequentially, but they do
	// share a common build directory. In rare cases, the
	// tests have stepped on each other here, most likely due
	// the test child process from a previous test not being
	// completely stopped before the next test starts.
	if _, err := os.Stat(fakeBin); err == nil {
		return
	} else if !os.IsNotExist(err) {
		t.Fatal(err)
	}

	testSource, err := os.Open(os.Args[0])
	if err != nil {
		t.Fatal(err)
	}
	defer testSource.Close()

	testBin, err := os.OpenFile(fakeBin, os.O_RDWR|os.O_CREATE, 0755)
	if err != nil {
		t.Fatal(err)
	}
	defer testBin.Close()

	_, err = io.Copy(testBin, testSource)
	if err != nil {
		t.Fatal(err)
	}
}

func TestRunnerContextExit(t *testing.T) {
	createFakeBinary(t)

	// set this to control the behavior in TestMain()
	os.Setenv(testModeVar, "RunnerContextExit")

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := MockConfig().
		WithEnvPassThrough(testModeVar)
	cfg.Index = 9

	runner := NewRunner(log, cfg)
	errOut := make(chan error)

	ctx, cancel := context.WithCancel(context.Background())
	if err := runner.Start(ctx, errOut); err != nil {
		t.Fatal(err)
	}
	// Add a short sleep to avoid canceling the context before the process
	// has launched. Would be better to do it in an onStarted() callback,
	// but implementing that to fix this seems like overkill.
	time.Sleep(10 * time.Millisecond)
	cancel()

	err := <-errOut
	if errors.Cause(err) == common.NormalExit {
		t.Fatal("expected process to not exit normally")
	}
}

func TestRunnerNormalExit(t *testing.T) {
	var bypass bool = false
	createFakeBinary(t)

	// set this to control the behavior in TestMain()
	os.Setenv(testModeVar, "RunnerNormalExit")
	// verify that bad user env is scrubbed
	os.Setenv("FI_VERBS_PREFER_XRC", "1")
	// verify that allowed user env gets overridden by config
	os.Setenv("OFI_INTERFACE", "bob0")
	// verify that allowed user env without a config override is passed through
	allowedUserEnv := "MOOD"
	allowedUserVal := "happy"
	os.Setenv(allowedUserEnv, allowedUserVal)

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	cfg := MockConfig().
		WithEnvPassThrough(testModeVar, "OFI_INTERFACE", allowedUserEnv).
		WithTargetCount(42).
		WithHelperStreamCount(1).
		WithFabricInterface("qib0").
		WithLogMask("DEBUG,MGMT=DEBUG,RPC=ERR,MEM=ERR").
		WithPinnedNumaNode(1).
		WithBypassHealthChk(&bypass).
		WithCrtCtxShareAddr(1).
		WithCrtTimeout(30).
		WithStorage(
			storage.NewTierConfig().
				WithStorageClass("ram").
				WithScmMountPoint("/foo/bar"),
		)
	runner := NewRunner(log, cfg)
	errOut := make(chan error)

	if err := runner.Start(context.Background(), errOut); err != nil {
		t.Fatal(err)
	}

	err := <-errOut
	if errors.Cause(err).Error() != common.NormalExit.Error() {
		t.Fatalf("expected normal exit; got %s", err)
	}

	// Light integration testing of arg/env generation; unit tests elsewhere.
	wantArgs := "-t 42 -x 1 -T 1 -p 1 -I 0 -r 0 -H 0 -s /foo/bar"
	var gotArgs string
	env := []string{
		"FI_OFI_RXM_USE_SRX=1",
		"CRT_CTX_SHARE_ADDR=1",
		"CRT_TIMEOUT=30",
		"OFI_INTERFACE=qib0",
		"D_LOG_MASK=DEBUG,MGMT=DEBUG,RPC=ERR,MEM=ERR",
		allowedUserEnv + "=" + allowedUserVal,
	}
	sort.Strings(env)
	wantEnv := strings.Join(env, " ")
	var gotEnv string

	splitLine := func(line, marker string, dest *string) {
		if strings.Contains(line, marker) {
			parts := strings.Split(line, testSep)
			if len(parts) != 2 {
				t.Fatalf("malformed line: %s", line)
			}
			*dest = parts[1]
		}
	}

	scanner := bufio.NewScanner(strings.NewReader(buf.String()))
	for scanner.Scan() {
		splitLine(scanner.Text(), testArgsStr, &gotArgs)
		splitLine(scanner.Text(), testEnvStr, &gotEnv)
	}
	if err := scanner.Err(); err != nil {
		t.Fatal(err)
	}
	if gotArgs != wantArgs {
		t.Fatalf("wanted %q; got %q", wantArgs, gotArgs)
	}
	if gotEnv != wantEnv {
		t.Fatalf("wanted %q; got %q", wantEnv, gotEnv)
	}
}
