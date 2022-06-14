//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func mkArgs(args ...string) []string {
	return args
}

func runTestCmd(t *testing.T, log *logging.LeveledLogger, args ...string) (*cliOptions, error) {
	t.Helper()

	test.SkipWithoutMockLibrary(t, "libdaos_mocks")

	var opts cliOptions
	return &opts, parseOpts(args, &opts, log)
}
