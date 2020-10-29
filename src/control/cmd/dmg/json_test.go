//
// (C) Copyright 2020 Intel Corporation.
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

package main

import (
	"bytes"
	"encoding/json"
	"io"
	"os"
	"path/filepath"
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

	aclPath := filepath.Join(os.TempDir(), "testACLFile.txt")
	createTestFile(t, aclPath, "A::OWNER@:rw\nA::user1@:rw\nA:g:group1@:r\n")
	defer os.Remove(aclPath)

	for _, args := range cmdArgs {
		t.Run(strings.Join(args, " "), func(t *testing.T) {
			testArgs := append([]string{"-i", "--json"}, args...)
			switch strings.Join(args, " ") {
			case "version":
				return
			case "storage prepare":
				testArgs = append(testArgs, "--force")
			case "storage query target-health":
				testArgs = append(testArgs, []string{"-r", "0", "-t", "0"}...)
			case "storage query device-health":
				testArgs = append(testArgs, []string{"-u", common.MockUUID()}...)
			case "storage set nvme-faulty":
				testArgs = append(testArgs, []string{"--force", "-u", common.MockUUID()}...)
			case "pool create":
				testArgs = append(testArgs, []string{"-s", "1TB"}...)
			case "pool destroy", "pool evict", "pool query", "pool get-acl":
				testArgs = append(testArgs, []string{"--pool", common.MockUUID()}...)
			case "pool overwrite-acl", "pool update-acl":
				testArgs = append(testArgs, []string{"--pool", common.MockUUID(), "-a", aclPath}...)
			case "pool delete-acl":
				testArgs = append(testArgs, []string{"--pool", common.MockUUID(), "-p", "foo@"}...)
			case "pool set-prop":
				testArgs = append(testArgs, []string{"--pool", common.MockUUID(), "-n", "foo", "-v", "bar"}...)
			case "pool extend":
				testArgs = append(testArgs, []string{"--pool", common.MockUUID(), "--ranks", "0", "-s", "1TB"}...)
			case "pool exclude", "pool drain", "pool reintegrate":
				testArgs = append(testArgs, []string{"--pool", common.MockUUID(), "--rank", "0"}...)
			case "cont set-owner":
				testArgs = append(testArgs, []string{"--user", "foo", "--pool", common.MockUUID(), "--cont", common.MockUUID()}...)
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
