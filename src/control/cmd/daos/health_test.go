//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/ui"
	"github.com/daos-stack/daos/src/control/logging"
)

func RunSelfTest(ctx context.Context, cfg *daos.SelfTestConfig) ([]*daos.SelfTestResult, error) {
	return []*daos.SelfTestResult{}, nil
}

func TestDaos_netTestCmdExecute(t *testing.T) {
	// Quickie smoke test for the UI -- will flesh out later.
	var opts cliOptions
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	args := []string{
		"health", "net-test",
		"--ranks", "0-3",
		"--tags", "4-9",
		"--size", "20 MiB",
		"--rep-count", "2222",
		"--bytes", "--verbose",
	}
	expArgs := netTestCmd{}
	expArgs.Ranks.Replace(ranklist.MustCreateRankSet("0-3"))
	expArgs.Tags.Replace(ranklist.MustCreateRankSet("4-9"))
	expArgs.XferSize.Bytes = 20 * humanize.MiByte
	expArgs.RepCount = 2222
	expArgs.Verbose = true
	expArgs.TpsBytes = true

	if err := parseOpts(args, &opts, log); err != nil {
		t.Fatal(err)
	}
	cmpOpts := cmp.Options{
		cmpopts.IgnoreUnexported(netTestCmd{}),
		cmp.Comparer(func(a, b ranklist.RankSet) bool {
			return a.String() == b.String()
		}),
		cmp.Comparer(func(a, b ui.ByteSizeFlag) bool {
			return a.String() == b.String()
		}),
		cmpopts.IgnoreTypes(cmdutil.LogCmd{}, cmdutil.JSONOutputCmd{}),
	}
	test.CmpAny(t, "health net-test args", expArgs, opts.Health.NetTest, cmpOpts...)
}
