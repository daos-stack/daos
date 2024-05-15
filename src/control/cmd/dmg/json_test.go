//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"encoding/json"
	"io"
	"os"
	"reflect"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
)

func walkStruct(v reflect.Value, prefix []string, visit func([]string)) {
	vType := v.Type()
	hasSub := false

	for i := 0; i < vType.NumField(); i++ {
		f := vType.Field(i)
		kind := f.Type.Kind()
		if kind != reflect.Struct {
			continue
		}

		cmd := f.Tag.Get("command")
		if cmd == "" {
			continue
		}

		hasSub = true
		subCmd := append(prefix, []string{cmd}...)
		walkStruct(v.Field(i), subCmd, visit)
	}

	if !hasSub {
		visit(prefix)
	}
}

func TestDmg_JsonOutput(t *testing.T) {
	var cmdArgs [][]string

	// Use reflection to build up a list of commands in order to
	// verify that they return valid JSON when invoked with valid
	// arguments. This should catch new commands added without proper
	// support for JSON output.
	walkStruct(reflect.ValueOf(cliOptions{}), nil, func(cmd []string) {
		cmdArgs = append(cmdArgs, cmd)
	})

	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()
	aclContent := "A::OWNER@:rw\nA::user1@:rw\nA:g:group1@:r\n"
	aclPath := test.CreateTestFile(t, testDir, aclContent)

	for _, args := range cmdArgs {
		t.Run(strings.Join(args, " "), func(t *testing.T) {
			testArgs := append([]string{"-i", "--json"}, args...)
			switch strings.Join(args, " ") {
			case "version", "telemetry config", "telemetry run", "config generate",
				"manpage", "system set-prop", "support collect-log":
				return
			case "storage nvme-rebind":
				testArgs = append(testArgs, "-l", "foo.com", "-a",
					test.MockPCIAddr())
			case "storage nvme-add-device":
				testArgs = append(testArgs, "-l", "foo.com", "-a",
					test.MockPCIAddr(), "-e", "0")
			case "storage query device-health":
				testArgs = append(testArgs, "-u", test.MockUUID())
			case "storage set nvme-faulty":
				testArgs = append(testArgs, "--force", "-u", test.MockUUID())
			case "storage replace nvme":
				testArgs = append(testArgs, "--old-uuid", test.MockUUID(),
					"--new-uuid", test.MockUUID())
			case "storage led identify", "storage led check", "storage led clear":
				testArgs = append(testArgs, test.MockUUID())
			case "pool create":
				testArgs = append(testArgs, "-s", "1TB", "label")
			case "pool destroy", "pool evict", "pool query", "pool get-acl", "pool upgrade":
				testArgs = append(testArgs, test.MockUUID())
			case "pool overwrite-acl", "pool update-acl":
				testArgs = append(testArgs, test.MockUUID(), "-a", aclPath)
			case "pool delete-acl":
				testArgs = append(testArgs, test.MockUUID(), "-p", "foo@")
			case "pool set-prop":
				testArgs = append(testArgs, test.MockUUID(), "label:foo")
			case "pool get-prop":
				testArgs = append(testArgs, test.MockUUID(), "label")
			case "pool extend":
				testArgs = append(testArgs, test.MockUUID(), "--ranks", "0")
			case "pool exclude", "pool drain", "pool reintegrate":
				testArgs = append(testArgs, test.MockUUID(), "--rank", "0")
			case "pool query-targets":
				testArgs = append(testArgs, test.MockUUID(), "--rank", "0", "--target-idx", "1,3,5,7")
			case "container set-owner":
				testArgs = append(testArgs, "--user", "foo", "--pool", test.MockUUID(),
					"--cont", test.MockUUID())
			case "telemetry metrics list", "telemetry metrics query":
				return // These commands query via http directly
			case "system cleanup":
				testArgs = append(testArgs, "hostname")
			case "system set-attr":
				testArgs = append(testArgs, "foo:bar")
			case "system del-attr":
				testArgs = append(testArgs, "foo")
			case "system exclude":
				testArgs = append(testArgs, "--ranks", "0")
			case "system clear-exclude":
				testArgs = append(testArgs, "--ranks", "0")
			}

			// replace os.Stdout so that we can verify the generated output
			var result bytes.Buffer
			r, w, _ := os.Pipe()
			done := make(chan struct{})
			go func() {
				_, _ = io.Copy(&result, r)
				close(done)
			}()
			stdout := os.Stdout
			defer func() {
				os.Stdout = stdout
			}()
			os.Stdout = w

			// Use a normal logger to verify that we don't mess up JSON output.
			log := logging.NewCommandLineLogger()

			ctlClient := control.DefaultMockInvoker(log)
			conn := newTestConn(t)
			bridge := &bridgeConnInvoker{
				MockInvoker: *ctlClient,
				t:           t,
				conn:        conn,
			}

			err := parseOpts(testArgs, &cliOptions{}, bridge, log)
			if err != nil {
				t.Errorf("%s: %s", strings.Join(testArgs, " "), err)
			}
			w.Close()
			<-done

			if !json.Valid(result.Bytes()) {
				t.Fatalf("invalid JSON in response: %s", result.String())
			}
		})
	}
}
