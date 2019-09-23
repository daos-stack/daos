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

package ioserver

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	testModeVar = "IOSERVER_TEST_MODE"
	testSep     = "==="
	testArgsStr = "IOSERVER_TEST_ARGS"
	testEnvStr  = "IOSERVER_TEST_ENV"
)

func TestMain(m *testing.M) {
	switch os.Getenv(testModeVar) {
	case "":
		os.Exit(m.Run())
	case "RunnerNormalExit":
		os.Unsetenv(testModeVar)
		fmt.Printf("%s%s%s\n", testEnvStr, testSep, strings.Join(os.Environ(), " "))
		fmt.Printf("%s%s%s\n", testArgsStr, testSep, strings.Join(os.Args[1:], " "))
		os.Exit(0)
	case "RunnerContextExit":
		time.Sleep(30 * time.Second)
		os.Exit(1)
	}
}

// createFakeBinary writes out a copy of this test file to
// an executable file in the test directory. This fake binary
// is named the same as a DAOS I/O server binary so that it's
// located via findBinary(). When it is invoked by Runner.run(),
// the TestMain() function is used to do some simple simulation
// of the real binary's behavior.
func createFakeBinary(t *testing.T) {
	testDir := filepath.Dir(os.Args[0])

	testSource, err := os.Open(os.Args[0])
	if err != nil {
		t.Fatal(err)
	}
	defer testSource.Close()

	testBin, err := os.OpenFile(path.Join(testDir, ioServerBin), os.O_RDWR|os.O_CREATE, 0755)
	if err != nil {
		t.Fatal(err)
	}
	defer testBin.Close()

	_, err = io.Copy(testBin, testSource)
	if err != nil {
		t.Fatal(err)
	}

	// ensure that we have a clean environment for testing
	os.Clearenv()
}

func TestRunnerContextExit(t *testing.T) {
	createFakeBinary(t)

	// set this to control the behavior in TestMain()
	os.Setenv(testModeVar, "RunnerContextExit")

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)()

	cfg := NewConfig()

	runner := NewRunner(log, cfg)
	errOut := make(chan error)

	ctx, cancel := context.WithCancel(context.Background())
	if err := runner.Start(ctx, errOut); err != nil {
		t.Fatal(err)
	}
	cancel()

	exitErr := <-errOut
	if errors.Cause(exitErr) == NormalExit {
		t.Fatal("expected process to not exit normally")
	}
}

func TestRunnerNormalExit(t *testing.T) {
	createFakeBinary(t)

	// set this to control the behavior in TestMain()
	os.Setenv(testModeVar, "RunnerNormalExit")
	// verify that user env gets overridden by config
	os.Setenv("OFI_INTERFACE", "bob0")

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)()

	cfg := NewConfig().
		WithTargetCount(42).
		WithHelperStreamCount(1).
		WithFabricInterface("qib0")
	runner := NewRunner(log, cfg)
	errOut := make(chan error)

	if err := runner.Start(context.Background(), errOut); err != nil {
		t.Fatal(err)
	}

	exitErr := <-errOut
	if errors.Cause(exitErr).Error() != NormalExit.Error() {
		t.Fatalf("expected normal exit; got %s", exitErr)
	}

	// Light integration testing of arg/env generation; unit tests elsewhere.
	wantArgs := "-t 42 -x 1"
	var gotArgs string
	wantEnv := "OFI_INTERFACE=qib0"
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
