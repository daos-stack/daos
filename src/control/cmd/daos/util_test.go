//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"reflect"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/logging"
)

func runCmdTest(t *testing.T, args []string, expCmd any, expErr error, cmdPath string, cmpOpts ...cmp.Option) {
	t.Helper()

	var opts cliOptions
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	if err := parseOpts(args, &opts, log); err != nil {
		test.CmpErr(t, expErr, err)
		if expErr != nil {
			return
		}
	}

	testCmd := reflect.ValueOf(opts)
	for _, subCmd := range strings.Split(cmdPath, ".") {
		testCmd = testCmd.FieldByName(subCmd)
		if !testCmd.IsValid() || testCmd.IsZero() {
			t.Fatalf("failed to select subcommand struct using %q", cmdPath)
		}
	}

	cmpOpts = append(cmpOpts, []cmp.Option{
		cmpopts.IgnoreUnexported(ui.GetPropertiesFlag{}, ui.SetPropertiesFlag{}, ui.PropertiesFlag{}),
		cmpopts.IgnoreUnexported(testCmd.Interface()),
		cmpopts.IgnoreTypes(cmdutil.LogCmd{}, cmdutil.JSONOutputCmd{}),
		cmp.Comparer(func(a, b ranklist.RankSet) bool {
			return a.String() == b.String()
		}),
		cmp.Comparer(func(a, b ui.ByteSizeFlag) bool {
			return a.String() == b.String()
		}),
	}...)
	test.CmpAny(t, fmt.Sprintf("%s args", cmdPath), expCmd, testCmd.Interface(), cmpOpts...)
}
