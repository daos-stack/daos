//
// (C) Copyright 2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

var (
	runSelfTestResult []*daos.SelfTestResult
	runSelfTestErr    error
)

func RunSelfTest(ctx context.Context, cfg *daos.SelfTestConfig) ([]*daos.SelfTestResult, error) {
	return runSelfTestResult, runSelfTestErr
}

func TestDaos_netTestCmdExecute(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "health", "net-test")

	for name, tc := range map[string]struct {
		args    []string
		expArgs netTestCmd
		expErr  error
	}{
		"all set (long)": {
			args: test.JoinArgs(baseArgs,
				"--ranks", "0-3",
				"--tags", "4-9",
				"--size", "20 MiB",
				"--rep-count", "2222",
				"--max-inflight", "1234",
				"--bytes", "--verbose",
			),
			expArgs: func() netTestCmd {
				cmd := netTestCmd{}
				cmd.Ranks.Replace(ranklist.MustCreateRankSet("0-3"))
				cmd.Tags.Replace(ranklist.MustCreateRankSet("4-9"))
				cmd.XferSize.Bytes = 20 * humanize.MiByte
				cmd.RepCount = 2222
				cmd.MaxInflight = 1234
				cmd.Verbose = true
				cmd.TpsBytes = true
				return cmd
			}(),
		},
		"all set (short)": {
			args: test.JoinArgs(baseArgs,
				"-r", "0-3",
				"-t", "4-9",
				"-s", "20 MiB",
				"-c", "2222",
				"-m", "1234",
				"-y", "-v",
			),
			expArgs: func() netTestCmd {
				cmd := netTestCmd{}
				cmd.Ranks.Replace(ranklist.MustCreateRankSet("0-3"))
				cmd.Tags.Replace(ranklist.MustCreateRankSet("4-9"))
				cmd.XferSize.Bytes = 20 * humanize.MiByte
				cmd.RepCount = 2222
				cmd.MaxInflight = 1234
				cmd.Verbose = true
				cmd.TpsBytes = true
				return cmd
			}(),
		},
		"selftest fails": {
			args:   []string{"health", "net-test"},
			expErr: errors.New("whoops"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.expErr != nil {
				prevErr := runSelfTestErr
				t.Cleanup(func() {
					runSelfTestErr = prevErr
				})
				runSelfTestErr = tc.expErr
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Health.NetTest")
		})
	}
}
