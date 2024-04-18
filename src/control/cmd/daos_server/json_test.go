//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

type jsonCmdTest struct {
	name         string
	cmd          string
	applyMocks   func(t *testing.T) // Apply mocking via function pointers when tests are run.
	cleanupMocks func(t *testing.T) // Cleanup mocking after each test.
	expOut       interface{}        // JSON encoded data should output.
	expErr       error
}

func runJSONCmdTests(t *testing.T, log *logging.LeveledLogger, buf fmt.Stringer, cmdTests []jsonCmdTest) {
	t.Helper()

	for _, tc := range cmdTests {
		t.Run(tc.name, func(t *testing.T) {
			t.Helper()
			defer test.ShowBufferOnFailure(t, buf)

			// Replace os.Stdout so that we can verify the generated output.
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

			if tc.applyMocks != nil && tc.cleanupMocks != nil {
				tc.applyMocks(t)
				defer tc.cleanupMocks(t)
			}

			var opts mainOpts
			test.CmpErr(t, tc.expErr, parseOpts(strings.Split(tc.cmd, " "), &opts, log))

			w.Close()
			<-done

			// Verify only JSON gets printed.
			if !json.Valid(result.Bytes()) {
				t.Fatalf("invalid JSON in response: %s", result.String())
			}

			var sb strings.Builder
			if err := cmdutil.OutputJSON(&sb, tc.expOut, tc.expErr); err != nil {
				if err != tc.expErr {
					t.Fatalf("OutputJSON: %s", err)
				}
			}

			if diff := cmp.Diff(sb.String(), result.String()); diff != "" {
				t.Fatalf("unexpected stdout (-want, +got):\n%s\n", diff)
			}
		})
	}
}
