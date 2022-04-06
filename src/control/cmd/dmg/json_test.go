//
// (C) Copyright 2020-2022 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/common"
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

	testDir, cleanup := common.CreateTestDir(t)
	defer cleanup()
	aclContent := "A::OWNER@:rw\nA::user1@:rw\nA:g:group1@:r\n"
	aclPath := common.CreateTestFile(t, testDir, aclContent)

	for _, args := range cmdArgs {
		t.Run(strings.Join(args, " "), func(t *testing.T) {
			testArgs := append([]string{"-i", "--json"}, args...)
			switch strings.Join(args, " ") {
			case "version", "telemetry config", "telemetry run", "config generate",
				"manpage":
				return
			case "storage nvme-rebind":
				testArgs = append(testArgs, "-l", "foo.com", "-a",
					common.MockPCIAddr())
			case "storage nvme-add-device":
				testArgs = append(testArgs, "-l", "foo.com", "-a",
					common.MockPCIAddr(), "-e", "0")
			case "storage query target-health":
				testArgs = append(testArgs, "-r", "0", "-t", "0")
			case "storage query device-health":
				testArgs = append(testArgs, "-u", common.MockUUID())
			case "storage set nvme-faulty":
				testArgs = append(testArgs, "--force", "-u", common.MockUUID())
			case "storage replace nvme":
				testArgs = append(testArgs, "--old-uuid", common.MockUUID(),
					"--new-uuid", common.MockUUID())
			case "storage identify vmd":
				testArgs = append(testArgs, "--uuid", common.MockUUID())
			case "pool create":
				testArgs = append(testArgs, "-s", "1TB")
			case "pool destroy", "pool evict", "pool query", "pool get-acl":
				testArgs = append(testArgs, common.MockUUID())
			case "pool overwrite-acl", "pool update-acl":
				testArgs = append(testArgs, common.MockUUID(), "-a", aclPath)
			case "pool delete-acl":
				testArgs = append(testArgs, common.MockUUID(), "-p", "foo@")
			case "pool set-prop":
				testArgs = append(testArgs, common.MockUUID(), "label:foo")
			case "pool get-prop":
				testArgs = append(testArgs, common.MockUUID(), "label")
			case "pool extend":
				testArgs = append(testArgs, common.MockUUID(), "--ranks", "0")
			case "pool exclude", "pool drain", "pool reintegrate":
				testArgs = append(testArgs, common.MockUUID(), "--rank", "0")
			case "container set-owner":
				testArgs = append(testArgs, "--user", "foo", "--pool", common.MockUUID(),
					"--cont", common.MockUUID())
			case "telemetry metrics list", "telemetry metrics query":
				return // These commands query via http directly
			case "system cleanup":
				testArgs = append(testArgs, "hostname")
			case "check repair":
				testArgs = append(testArgs, "1", "2")
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
