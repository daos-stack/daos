//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/daos/api"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

var (
	defaultPoolInfo *daos.PoolInfo = &daos.PoolInfo{
		QueryMask:       daos.DefaultPoolQueryMask,
		State:           daos.PoolServiceStateReady,
		UUID:            test.MockPoolUUID(1),
		Label:           "test-pool",
		TotalTargets:    48,
		TotalEngines:    3,
		ActiveTargets:   48,
		DisabledTargets: 0,
		Version:         1,
		ServiceLeader:   2,
		ServiceReplicas: []ranklist.Rank{0, 1, 2},
		TierStats: []*daos.StorageUsageStats{
			{
				MediaType: daos.StorageMediaTypeScm,
				Total:     64 * humanize.TByte,
				Free:      16 * humanize.TByte,
			},
			{
				MediaType: daos.StorageMediaTypeNvme,
				Total:     1 * humanize.PByte,
				Free:      512 * humanize.TByte,
			},
		},
	}
)

var (
	defaultGetPoolListResult = []*daos.PoolInfo{
		defaultPoolInfo,
	}

	getPoolListResult []*daos.PoolInfo = defaultGetPoolListResult
	getPoolListErr    error
)

func GetPoolList(ctx context.Context, req api.GetPoolListReq) ([]*daos.PoolInfo, error) {
	return getPoolListResult, getPoolListErr
}

func TestDaos_poolListCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "pool", "list")

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs poolListCmd
	}{
		"all set (long)": {
			args: test.JoinArgs(baseArgs, "--verbose", "--no-query"),
			expArgs: poolListCmd{
				NoQuery: true,
				Verbose: true,
			},
		},
		"all set (short)": {
			args: test.JoinArgs(baseArgs, "-v", "-n"),
			expArgs: poolListCmd{
				NoQuery: true,
				Verbose: true,
			},
		},
		"query fails": {
			args:   []string{"pool", "list"},
			expErr: errors.New("whoops"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.expErr != nil {
				prevErr := getPoolListErr
				t.Cleanup(func() {
					getPoolListErr = prevErr
				})
				getPoolListErr = tc.expErr
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Pool.List")
		})
	}
}

var (
	poolConnectErr error
)

func PoolConnect(ctx context.Context, req api.PoolConnectReq) (*api.PoolConnectResp, error) {
	return &api.PoolConnectResp{
		Connection: api.MockPoolHandle(),
		Info:       defaultPoolInfo,
	}, poolConnectErr
}

func TestDaos_poolQueryCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "pool", "query", defaultPoolInfo.Label)

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs poolQueryCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"missing pool ID": {
			args:   baseArgs[:len(baseArgs)-1],
			expErr: errors.New("no pool UUID or label supplied"),
		},
		"connect fails": {
			args:   baseArgs,
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := poolConnectErr
				t.Cleanup(func() {
					poolConnectErr = prevErr
				})
				poolConnectErr = errors.New("whoops")
			},
		},
		"all set (long)": {
			args: test.JoinArgs(baseArgs, "--show-enabled", "--health-only"),
			expArgs: poolQueryCmd{
				ShowEnabledRanks: true,
				HealthOnly:       true,
			},
		},
		"all set (short)": {
			args: test.JoinArgs(baseArgs, "-e", "-t"),
			expArgs: poolQueryCmd{
				ShowEnabledRanks: true,
				HealthOnly:       true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Pool.Query")
		})
	}
}

func TestDaos_poolQueryTargetsCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "pool", "query-targets", defaultPoolInfo.Label)

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs poolQueryTargetsCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--rank=2", "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"missing pool ID": {
			args:   test.JoinArgs(baseArgs[:len(baseArgs)-1], "--rank=2"),
			expErr: errors.New("no pool UUID or label supplied"),
		},
		"missing rank argument": {
			args:   baseArgs,
			expErr: errors.New("required flag"),
		},
		"connect fails": {
			args:   test.JoinArgs(baseArgs, "--rank=2"),
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := poolConnectErr
				t.Cleanup(func() {
					poolConnectErr = prevErr
				})
				poolConnectErr = errors.New("whoops")
			},
		},
		"success (rank only)": {
			args: test.JoinArgs(baseArgs, "--rank=2"),
			expArgs: poolQueryTargetsCmd{
				Rank: 2,
			},
		},
		"success (rank and target)": {
			args: test.JoinArgs(baseArgs, "--rank=2", "--target-idx=1,2"),
			expArgs: poolQueryTargetsCmd{
				Rank: 2,
				Targets: ui.RankSetFlag{
					RankSet: *ranklist.MustCreateRankSet("1,2"),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Pool.QueryTargets")
		})
	}
}

func TestDaos_poolSetAttrCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "pool", "set-attr", defaultPoolInfo.Label)
	keysOnlyArg := "key1,key2"
	keyValArg := "key1:val1,key2:val2"

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs poolSetAttrCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad", keyValArg),
			expErr: errors.New("unknown flag"),
		},
		"connect fails": {
			args:   test.JoinArgs(baseArgs, keyValArg),
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := poolConnectErr
				t.Cleanup(func() {
					poolConnectErr = prevErr
				})
				poolConnectErr = errors.New("whoops")
			},
		},
		"missing required arguments": {
			args:   baseArgs,
			expErr: errors.New("required argument"),
		},
		"malformed required arguments": {
			args:   test.JoinArgs(baseArgs, keysOnlyArg),
			expErr: errors.New("invalid property"),
		},
		"success": {
			args: test.JoinArgs(baseArgs, keyValArg),
			expArgs: poolSetAttrCmd{
				Args: struct {
					Attrs ui.SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]" required:"1"`
				}{
					Attrs: ui.SetPropertiesFlag{
						ParsedProps: map[string]string{
							"key1": "val1",
							"key2": "val2",
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Pool.SetAttr")
		})
	}
}

func TestDaos_poolGetAttrCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "pool", "get-attr", defaultPoolInfo.Label)
	keysOnlyArg := "key1,key2"

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs poolGetAttrCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"missing pool ID": {
			args:   baseArgs[:len(baseArgs)-1],
			expErr: errors.New("no pool UUID or label supplied"),
		},
		"connect fails": {
			args:   baseArgs,
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := poolConnectErr
				t.Cleanup(func() {
					poolConnectErr = prevErr
				})
				poolConnectErr = errors.New("whoops")
			},
		},
		"malformed arguments": {
			args:   test.JoinArgs(baseArgs, strings.ReplaceAll(keysOnlyArg, ",", ":")),
			expErr: errors.New("key cannot contain"),
		},
		"unknown key(s)": {
			args:   test.JoinArgs(baseArgs, keysOnlyArg),
			expErr: daos.Nonexistent,
		},
		"success (one key)": {
			args: test.JoinArgs(baseArgs, "one"),
			expArgs: poolGetAttrCmd{
				Args: struct {
					Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
				}{
					Attrs: ui.GetPropertiesFlag{
						ParsedProps: map[string]struct{}{
							"one": {},
						},
					},
				},
			},
		},
		"success (all keys)": {
			args:    baseArgs,
			expArgs: poolGetAttrCmd{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Pool.GetAttr")
		})
	}
}

func TestDaos_poolDelAttrCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "pool", "del-attr", defaultPoolInfo.Label)
	keysOnlyArg := "key1,key2"

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs poolDelAttrCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"missing required arguments": {
			args:   baseArgs,
			expErr: errors.New("required argument"),
		},
		"connect fails": {
			args:   test.JoinArgs(baseArgs, keysOnlyArg),
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := poolConnectErr
				t.Cleanup(func() {
					poolConnectErr = prevErr
				})
				poolConnectErr = errors.New("whoops")
			},
		},
		"malformed arguments": {
			args:   test.JoinArgs(baseArgs, strings.ReplaceAll(keysOnlyArg, ",", ":")),
			expErr: errors.New("key cannot contain"),
		},
		"success (one key)": {
			args: test.JoinArgs(baseArgs, "one"),
			expArgs: poolDelAttrCmd{
				Args: struct {
					Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]" required:"1"`
				}{
					Attrs: ui.GetPropertiesFlag{
						ParsedProps: map[string]struct{}{
							"one": {},
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Pool.DelAttr")
		})
	}
}

func TestDaos_poolListAttrCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "pool", "list-attr", defaultPoolInfo.Label)

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs poolListAttrsCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"missing pool ID": {
			args:   baseArgs[:len(baseArgs)-1],
			expErr: errors.New("no pool UUID or label supplied"),
		},
		"connect fails": {
			args:   baseArgs,
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := poolConnectErr
				t.Cleanup(func() {
					poolConnectErr = prevErr
				})
				poolConnectErr = errors.New("whoops")
			},
		},
		"success": {
			args:    baseArgs,
			expArgs: poolListAttrsCmd{},
		},
		"success (verbose, short)": {
			args: test.JoinArgs(baseArgs, "-V"),
			expArgs: poolListAttrsCmd{
				Verbose: true,
			},
		},
		"success (verbose, long)": {
			args: test.JoinArgs(baseArgs, "--verbose"),
			expArgs: poolListAttrsCmd{
				Verbose: true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Pool.ListAttrs")
		})
	}
}
