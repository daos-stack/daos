//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	sectest "github.com/daos-stack/daos/src/control/security/test"
	"github.com/google/uuid"
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

	caCertPath, caKeyPath := sectest.WriteCAFiles(t, testDir)
	// issue and generate-cert find the pool CA certificate beside its key
	// under the pool's own file names.
	poolCAPEM, poolCAKey := sectest.NewCA(t, "Pool CA", nil, nil)
	poolCAKeyDER, err := x509.MarshalPKCS8PrivateKey(poolCAKey)
	if err != nil {
		t.Fatal(err)
	}
	poolCACertPath, poolCAKeyPath := security.PoolCAPaths(filepath.Join(testDir, "pools"), uuid.MustParse(test.MockUUID()))
	if err := os.MkdirAll(filepath.Dir(poolCAKeyPath), 0700); err != nil {
		t.Fatal(err)
	}
	sectest.WriteCertFiles(t, poolCACertPath, poolCAKeyPath, poolCAPEM,
		pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: poolCAKeyDER}))

	for _, args := range cmdArgs {
		t.Run(strings.Join(args, " "), func(t *testing.T) {
			testArgs := append([]string{"-i", "--json"}, args...)
			var caBundle []byte
			switch strings.Join(args, " ") {
			case "version", "telemetry config", "telemetry run", "config generate",
				"manpage", "system set-prop", "support collect-log", "check repair":
				return
			case "pool node-auth enable", "pool node-auth add-ca":
				testArgs = append(testArgs, test.MockUUID(),
					"--cert", caCertPath)
			case "pool node-auth generate-ca":
				testArgs = append(testArgs, test.MockUUID(),
					"--daos-ca-cert", caCertPath, "--daos-ca-key", caKeyPath,
					"--output", filepath.Join(testDir, "pool_ca"))
			case "pool node-auth generate-cert":
				testArgs = append(testArgs, test.MockUUID(),
					"--pool-ca-key", poolCAKeyPath,
					"--node", "testnode",
					"--output", filepath.Join(testDir, "offline_certs"))
			case "pool node-auth issue":
				caBundle = poolCAPEM
				testArgs = append(testArgs, test.MockUUID(),
					"--pool-ca-key", poolCAKeyPath,
					"--node", "testnode",
					"--output", filepath.Join(testDir, "client_certs"))
			case "storage nvme-rebind":
				testArgs = append(testArgs, "-l", "foo.com", "-a",
					test.MockPCIAddr())
			case "storage nvme-add-device":
				testArgs = append(testArgs, "-l", "foo.com", "-a",
					test.MockPCIAddr(), "-e", "0")
			case "storage set nvme-faulty":
				testArgs = append(testArgs, "--host", "foo.com", "--force", "-u",
					test.MockUUID())
			case "storage replace nvme":
				testArgs = append(testArgs, "--host", "foo.com", "--old-uuid",
					test.MockUUID(), "--new-uuid", test.MockUUID())
			case "storage led identify", "storage led check", "storage led clear":
				testArgs = append(testArgs, test.MockUUID())
			case "pool create":
				testArgs = append(testArgs, "-s", "1TB", "label")
			case "pool destroy", "pool evict", "pool query", "pool get-acl", "pool upgrade",
				"pool rebuild start", "pool rebuild stop",
				"pool node-auth status", "pool node-auth disable":
				testArgs = append(testArgs, test.MockUUID())
			case "pool node-auth remove-ca":
				testArgs = append(testArgs, test.MockUUID(), "--fingerprint", "abc123")
			case "pool node-auth revoke":
				testArgs = append(testArgs, test.MockUUID(), "--node", "testnode")
			case "pool overwrite-acl", "pool update-acl":
				testArgs = append(testArgs, test.MockUUID(), "-a", aclPath)
			case "pool delete-acl":
				testArgs = append(testArgs, test.MockUUID(), "-p", "foo@")
			case "pool set-prop":
				testArgs = append(testArgs, test.MockUUID(), "label:foo")
			case "pool get-prop":
				testArgs = append(testArgs, test.MockUUID(), "label")
			case "pool extend", "pool exclude", "pool drain", "pool reintegrate":
				testArgs = append(testArgs, test.MockUUID(), "--ranks", "0")
			case "pool query-targets":
				testArgs = append(testArgs, test.MockUUID(), "--rank", "0", "--target-idx", "1,3,5,7")
			case "container set-owner":
				testArgs = append(testArgs, "--user", "foo", test.MockUUID(), test.MockUUID())
			case "telemetry metrics list", "telemetry metrics query":
				return // These commands query via http directly
			case "system cleanup":
				testArgs = append(testArgs, "hostname")
			case "check set-policy":
				testArgs = append(testArgs, "POOL_BAD_LABEL:IGNORE")
			case "system set-attr":
				testArgs = append(testArgs, "foo:bar")
			case "system del-attr":
				testArgs = append(testArgs, "foo")
			case "system exclude", "system clear-exclude", "system drain",
				"system reintegrate":
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
				caBundle:    caBundle,
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
