//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

type cmdTest struct {
	args   []string
	expErr error
}

func testFilesystemCmd(t *testing.T, baseArgs []string, extraTests map[string]cmdTest) {
	t.Helper()

	testCases := map[string]cmdTest{
		"pool, container": {
			args: mkArgs("test-pool", "test-cont"),
		},
		"pool, container-uuid": {
			args: mkArgs("test-pool", test.MockUUID()),
		},
		"pool, container, extra": {
			args:   mkArgs("test-pool", "test-cont", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"no pool/cont IDs or dfs path": {
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs"),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs"),
		},
		"namespace path with pool flag (deprecated) and container flag (deprecated)": {
			args:   mkArgs("--path", "/tmp/dfs", "--pool", "test-pool", "--cont", "test-cont"),
			expErr: errors.New("--path flag may not be set"),
		},
		"pool flag (deprecated), container": {
			args:   mkArgs("--pool", "test-pool", "test-cont"),
			expErr: errors.New("--pool flag requires --cont"),
		},
		"pool, container flag (deprecated)": {
			args:   mkArgs("test-pool", "--cont", "test-cont"),
			expErr: errors.New("--cont flag requires --pool"),
		},
		"dfs-path without pool/container": {
			args:   mkArgs("--dfs-path", "/tmp/dfs"),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"dfs-path with pool/container": {
			args: mkArgs("--dfs-path", "/tmp/dfs", "test-pool", "test-cont"),
		},
		"dfs-path with path": {
			args:   mkArgs("--dfs-path", "/tmp/dfs", "--path", "/tmp/dfs"),
			expErr: errors.New("both --dfs-path and --path"),
		},
		"dfs-prefix without pool/container": {
			args:   mkArgs("--dfs-prefix", "/tmp/dfs"),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"dfs-prefix with pool/container": {
			args: mkArgs("--dfs-prefix", "/tmp/dfs", "test-pool", "test-cont"),
		},
		"dfs-prefix with path": {
			args:   mkArgs("--dfs-prefix", "/tmp/dfs", "--path", "/tmp/dfs"),
			expErr: errors.New("both --dfs-prefix and --path"),
		},
		"dfs-path and dfs-prefix with pool/container": {
			args: mkArgs("--dfs-path", "/tmp/dfs", "--dfs-prefix", "/tmp/dfs", "test-pool", "test-cont"),
		},
	}
	for k, v := range extraTests {
		testCases[k] = v
	}

	for name, tc := range testCases {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			_, gotErr := runTestCmd(t, log, append(baseArgs, tc.args...)...)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestDaos_Filesystem_GetAttr(t *testing.T) {
	testFilesystemCmd(t, mkArgs("filesystem", "get-attr"), nil)
}

func TestDaos_Filesystem_ResetAttr(t *testing.T) {
	testFilesystemCmd(t, mkArgs("filesystem", "reset-attr"), nil)
}

func TestDaos_Filesystem_ResetChunkSize(t *testing.T) {
	testFilesystemCmd(t, mkArgs("filesystem", "reset-chunk-size"), nil)
}

func TestDaos_Filesystem_ResetOclass(t *testing.T) {
	testFilesystemCmd(t, mkArgs("filesystem", "reset-oclass"), nil)
}

func TestDaos_Filesystem_SetAttr(t *testing.T) {
	extraTests := map[string]cmdTest{
		"invalid oclass": {
			args:   mkArgs("pool", "cont", "--oclass", "invalid"),
			expErr: errors.New("unknown object class"),
		},
		"oclass (long)": {
			args: mkArgs("pool", "cont", "--oclass", "S1"),
		},
		"oclass (short)": {
			args: mkArgs("pool", "cont", "-o", "S2"),
		},
		"invalid chunk size": {
			args:   mkArgs("pool", "cont", "--chunk-size", "invalid"),
			expErr: errors.New("invalid chunk size"),
		},
		"chunk-size (long)": {
			args: mkArgs("pool", "cont", "--chunk-size", "1MB"),
		},
		"chunk-size (short)": {
			args: mkArgs("pool", "cont", "-z", "1kib"),
		},
	}

	testFilesystemCmd(t, mkArgs("filesystem", "set-attr"), extraTests)
}
