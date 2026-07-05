//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// extractInfoMessages extracts INFO level messages from a log buffer, returning just
// the message content without timestamps or prefixes. Returns a single string with
// messages joined by newlines, terminated with a newline if any messages were found.
func extractInfoMessages(logOutput string) string {
	var infoLines []string
	for _, line := range strings.Split(logOutput, "\n") {
		if strings.Contains(line, " INFO ") {
			// Extract just the message part after the timestamp
			// Format: "prefix INFO YYYY/MM/DD HH:MM:SS message"
			parts := strings.SplitN(line, " INFO ", 2)
			if len(parts) == 2 {
				// Skip the date and time to get just the message
				// After " INFO ", we have "YYYY/MM/DD HH:MM:SS message"
				remainder := parts[1]

				// Find the message by skipping two space-separated fields (date and time)
				fields := strings.SplitN(remainder, " ", 3)
				if len(fields) == 3 {
					// fields[0] = date, fields[1] = time, fields[2] = message
					infoLines = append(infoLines, fields[2])
				}
			}
		}
	}
	if len(infoLines) == 0 {
		return ""
	}
	return strings.Join(infoLines, "\n") + "\n"
}

func TestDmg_SystemCommands(t *testing.T) {
	withRanks := func(req control.UnaryRequest, ranks ...ranklist.Rank) control.UnaryRequest {
		if rs, ok := req.(interface{ SetRanks(*ranklist.RankSet) }); ok {
			rs.SetRanks(ranklist.RankSetFromRanks(ranks))
		}

		return req
	}

	withHosts := func(req control.UnaryRequest, hosts ...string) control.UnaryRequest {
		if rs, ok := req.(interface{ SetHosts(*hostlist.HostSet) }); ok {
			rs.SetHosts(hostlist.MustCreateSet(strings.Join(hosts, ",")))
		}

		return req
	}

	withSystem := func(req control.UnaryRequest, sys string) control.UnaryRequest {
		if rs, ok := req.(interface{ SetSystem(string) }); ok {
			rs.SetSystem(sys)
		}

		return req
	}

	runCmdTests(t, []cmdTest{
		{
			"system query with no arguments",
			"system query",
			strings.Join([]string{
				printRequest(t, &control.SystemQueryReq{}),
			}, " "),
			nil,
		},
		{
			"system query with single rank",
			"system query --ranks 0",
			strings.Join([]string{
				printRequest(t, withRanks(&control.SystemQueryReq{}, 0)),
			}, " "),
			nil,
		},
		{
			"system query with multiple ranks",
			"system query --ranks 0,2,4-8",
			strings.Join([]string{
				printRequest(t, withRanks(&control.SystemQueryReq{}, 0, 2, 4, 5, 6, 7, 8)),
			}, " "),
			nil,
		},
		{
			"system query with bad ranklist",
			"system query --ranks 0,2,four,,4-8",
			"",
			errors.Errorf("creating numeric set from "),
		},
		{
			"system query with single host",
			"system query --rank-hosts foo-0",
			strings.Join([]string{
				printRequest(t, withHosts(&control.SystemQueryReq{}, "foo-0")),
			}, " "),
			nil,
		},
		{
			"system query with multiple hosts",
			"system query --rank-hosts bar9,foo-[0-100]",
			strings.Join([]string{
				printRequest(t, withHosts(&control.SystemQueryReq{}, "foo-[0-100]", "bar9")),
			}, " "),
			nil,
		},
		{
			"system query with bad hostlist",
			"system query --rank-hosts bar9,foo-[0-100],123",
			"",
			errors.New(`invalid hostname "123"`),
		},
		{
			"system query with both hosts and ranks specified",
			"system query --rank-hosts bar9,foo-[0-100] --ranks 0,2,4-8",
			"",
			errors.New("--ranks and --rank-hosts options cannot be set together"),
		},
		{
			"system query with not-ok specified",
			"system query --not-ok",
			strings.Join([]string{
				printRequest(t, &control.SystemQueryReq{
					NotOK: true,
				}),
			}, " "),
			nil,
		},
		{
			"system query with states specified",
			"system query --with-states joined,Excluded",
			strings.Join([]string{
				printRequest(t, &control.SystemQueryReq{
					WantedStates: system.MemberStateJoined | system.MemberStateExcluded,
				}),
			}, " "),
			nil,
		},
		{
			"system query with invalid state specified",
			"system query --with-states Joined,Exclud",
			"",
			errors.New("invalid state name"),
		},
		{
			"system query with both not-ok and with-states specified",
			"system query --not-ok --with-states Joined",
			"",
			errors.New("--not-ok and --with-states options cannot be set together"),
		},
		{
			"system query verbose",
			"system query --verbose",
			strings.Join([]string{
				printRequest(t, &control.SystemQueryReq{}),
			}, " "),
			nil,
		},
		{
			"system stop with no arguments",
			"system stop",
			strings.Join([]string{
				printRequest(t, &control.SystemStopReq{}),
			}, " "),
			nil,
		},
		{
			"system stop with force",
			"system stop --force",
			strings.Join([]string{
				printRequest(t, &control.SystemStopReq{Force: true}),
			}, " "),
			nil,
		},
		{
			"system stop with single rank",
			"system stop --ranks 0",
			strings.Join([]string{
				printRequest(t, withRanks(&control.SystemStopReq{}, 0)),
			}, " "),
			nil,
		},
		{
			"system stop with multiple ranks",
			"system stop --ranks 0,1,4",
			strings.Join([]string{
				printRequest(t, withRanks(&control.SystemStopReq{}, 0, 1, 4)),
			}, " "),
			nil,
		},
		{
			"system stop with multiple hosts",
			"system stop --rank-hosts bar9,foo-[0-100]",
			strings.Join([]string{
				printRequest(t, withHosts(&control.SystemStopReq{}, "foo-[0-100]", "bar9")),
			}, " "),
			nil,
		},
		{
			"system stop with bad hostlist",
			"system stop --rank-hosts bar9,foo-[0-100],123",
			"",
			errors.New(`invalid hostname "123"`),
		},
		{
			"system stop with both hosts and ranks specified",
			"system stop --rank-hosts bar9,foo-[0-100] --ranks 0,2,4-8",
			"",
			errors.New("--ranks and --rank-hosts options cannot be set together"),
		},
		{
			"system stop with full option",
			"system stop --full",
			strings.Join([]string{
				printRequest(t, &control.SystemStopReq{Full: true}),
			}, " "),
			nil,
		},
		{
			"system stop with full and force options",
			"system stop --full --force",
			"",
			errors.New(`may not be mixed`),
		},
		{
			"system stop with full and rank-hosts options",
			"system stop --full --rank-hosts foo-[0-2]",
			"",
			errors.New(`may not be mixed`),
		},
		{
			"system stop with full and ranks options",
			"system stop --full --ranks 0-2",
			"",
			errors.New(`may not be mixed`),
		},
		{
			"system stop with ignore-admin-excluded option",
			"system stop --ignore-admin-excluded",
			strings.Join([]string{
				printRequest(t, &control.SystemStopReq{
					IgnoreAdminExcluded: true,
				}),
			}, " "),
			nil,
		},
		{
			"system start with no arguments",
			"system start",
			strings.Join([]string{
				printRequest(t, &control.SystemStartReq{}),
			}, " "),
			nil,
		},
		{
			"system start with single rank",
			"system start --ranks 0",
			strings.Join([]string{
				printRequest(t, withRanks(&control.SystemStartReq{}, 0)),
			}, " "),
			nil,
		},
		{
			"system start with multiple ranks",
			"system start --ranks 0,1,4",
			strings.Join([]string{
				printRequest(t, withRanks(&control.SystemStartReq{}, 0, 1, 4)),
			}, " "),
			nil,
		},
		{
			"system start with multiple hosts",
			"system start --rank-hosts bar9,foo-[0-100]",
			strings.Join([]string{
				printRequest(t, withHosts(&control.SystemStartReq{}, "foo-[0-100]", "bar9")),
			}, " "),
			nil,
		},
		{
			"system start with bad hostlist",
			"system start --rank-hosts bar9,foo-[0-100],123",
			"",
			errors.New(`invalid hostname "123"`),
		},
		{
			"system start with both hosts and ranks specified",
			"system start --rank-hosts bar9,foo-[0-100] --ranks 0,2,4-8",
			"",
			errors.New("--ranks and --rank-hosts options cannot be set together"),
		},
		{
			"system start with ranks and ignore-admin-excluded",
			"system start --ranks 0-3 --ignore-admin-excluded",
			strings.Join([]string{
				printRequest(t, withRanks(&control.SystemStartReq{IgnoreAdminExcluded: true}, 0, 1, 2, 3)),
			}, " "),
			nil,
		},
		{
			"system start with hosts and ignore-admin-excluded",
			"system start --rank-hosts foo-1 --ignore-admin-excluded",
			strings.Join([]string{
				printRequest(t, withHosts(&control.SystemStartReq{IgnoreAdminExcluded: true}, "foo-1")),
			}, " "),
			nil,
		},
		{
			"system start all with ignore-admin-excluded",
			"system start --ignore-admin-excluded",
			strings.Join([]string{
				printRequest(t, &control.SystemStartReq{IgnoreAdminExcluded: true}),
			}, " "),
			nil,
		},
		{
			"system exclude with multiple ranks",
			"system exclude --ranks 0,1,4",
			strings.Join([]string{
				printRequest(t, withRanks(&control.SystemExcludeReq{}, 0, 1, 4)),
			}, " "),
			nil,
		},
		{
			"system exclude with no ranks",
			"system exclude",
			"",
			errNoRanks,
		},
		{
			"system drain with multiple hosts",
			"system drain --rank-hosts foo-[0,1,4]",
			strings.Join([]string{
				printRequest(t, withSystem(
					withHosts(&control.SystemDrainReq{}, "foo-[0-1,4]"),
					"daos_server")),
			}, " "),
			nil,
		},
		{
			"system drain with multiple ranks",
			"system drain --ranks 0,1,4",
			strings.Join([]string{
				printRequest(t, withSystem(
					withRanks(&control.SystemDrainReq{}, 0, 1, 4),
					"daos_server")),
			}, " "),
			nil,
		},
		{
			"system drain without ranks",
			"system drain",
			"",
			errNoRanks,
		},
		{
			"system reintegrate with multiple hosts",
			"system reintegrate --rank-hosts foo-[0,1,4]",
			strings.Join([]string{
				printRequest(t, withSystem(
					withHosts(&control.SystemDrainReq{Reint: true}, "foo-[0-1,4]"),
					"daos_server")),
			}, " "),
			nil,
		},
		{
			"system reintegrate with multiple ranks",
			"system reintegrate --ranks 0,1,4",
			strings.Join([]string{
				printRequest(t, withSystem(
					withRanks(&control.SystemDrainReq{Reint: true}, 0, 1, 4),
					"daos_server")),
			}, " "),
			nil,
		},
		{
			"system reintegrate without ranks",
			"system reint", // Verify alias is accepted.
			"",
			errNoRanks,
		},
		{
			"system cleanup with machine name",
			"system cleanup foo1",
			strings.Join([]string{
				printRequest(t, withSystem(&control.SystemCleanupReq{
					Machine: "foo1",
				}, "daos_server")),
			}, " "),
			nil,
		},
		{
			"system cleanup without machine name",
			"system cleanup -v",
			"",
			errors.New("not provided"),
		},
		{
			"system rebuild start",
			"system rebuild start",
			strings.Join([]string{
				printRequest(t, &control.SystemRebuildManageReq{
					OpCode: control.PoolRebuildOpCodeStart,
				}),
			}, " "),
			nil,
		},
		{
			"system rebuild start with force",
			"system rebuild start --force",
			"",
			errors.New("unknown flag"),
		},
		{
			"system rebuild stop",
			"system rebuild stop",
			strings.Join([]string{
				printRequest(t, &control.SystemRebuildManageReq{
					OpCode: control.PoolRebuildOpCodeStop,
				}),
			}, " "),
			nil,
		},
		{
			"system rebuild stop with force",
			"system rebuild stop --force",
			strings.Join([]string{
				printRequest(t, &control.SystemRebuildManageReq{
					OpCode: control.PoolRebuildOpCodeStop,
					Force:  true,
				}),
			}, " "),
			nil,
		},
		{
			"system self-heal evaluate",
			"system self-heal eval",
			strings.Join([]string{
				printRequest(t, &control.SystemSelfHealEvalReq{}),
			}, " "),
			nil,
		},
		{
			"leader query",
			"system leader-query",
			strings.Join([]string{
				printRequest(t, &control.LeaderQueryReq{}),
				printRequest(t, &control.LeaderQueryReq{}),
			}, " "),
			nil,
		},
		{
			"system list-pools with default config",
			"system list-pools",
			strings.Join([]string{
				printRequest(t, &control.ListPoolsReq{}),
			}, " "),
			nil,
		},
		{
			"system set-attr multi attributes",
			"system set-attr foo:bar,baz:qux",
			strings.Join([]string{
				printRequest(t, &control.SystemSetAttrReq{
					Attributes: map[string]string{
						"foo": "bar",
						"baz": "qux",
					},
				}),
			}, " "),
			nil,
		},
		{
			"system get-attr multi attributes",
			"system get-attr foo,baz",
			strings.Join([]string{
				printRequest(t, &control.SystemGetAttrReq{
					Keys: []string{"baz", "foo"},
				}),
			}, " "),
			nil,
		},
		{
			"system del-attr multi attributes",
			"system del-attr foo,baz",
			strings.Join([]string{
				printRequest(t, &control.SystemSetAttrReq{
					Attributes: map[string]string{
						"foo": "",
						"baz": "",
					},
				}),
			}, " "),
			nil,
		},
		{
			"system get-prop multi props",
			"system get-prop daos_system,daos_version",
			strings.Join([]string{
				printRequest(t, &control.SystemGetPropReq{
					Keys: []daos.SystemPropertyKey{
						daos.SystemPropertyDaosSystem,
						daos.SystemPropertyDaosVersion,
					},
				}),
			}, " "),
			nil,
		},
		{
			"Non-existent subcommand",
			"system quack",
			"",
			fmt.Errorf("Unknown command"),
		},
		{
			"Non-existent option",
			"system start --rank 0",
			"",
			errors.New("unknown flag `rank'"),
		},
	})
}

func TestDmg_leaderQueryCmd(t *testing.T) {
	for name, tc := range map[string]struct {
		ctlCfg *control.Config
		resp   *mgmtpb.LeaderQueryResp
		msErr  error
		expErr error
	}{
		"no config": {
			resp:   &mgmtpb.LeaderQueryResp{},
			expErr: errors.New("leader query failed: no configuration loaded"),
		},
		"success": {
			ctlCfg: &control.Config{},
			resp:   &mgmtpb.LeaderQueryResp{},
		},
		"ms failures": {
			ctlCfg: &control.Config{},
			resp:   &mgmtpb.LeaderQueryResp{},
			msErr:  errors.New("failed"),
			expErr: errors.New("failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("10.0.0.1:10001",
					tc.msErr, tc.resp),
			})

			leaderQueryCmd := new(leaderQueryCmd)
			leaderQueryCmd.setInvoker(mi)
			leaderQueryCmd.SetLog(log)
			leaderQueryCmd.setConfig(tc.ctlCfg)

			gotErr := leaderQueryCmd.Execute(nil)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

// TestDmg_systemStartCmd covers case where duplicate rank result is detected when constructing
// rank groups, this applies across start stop exclude and any other commands that receive rank
// results in response but the test case isn't run individually for all those commands that share
// the behavior.
func TestDmg_systemStartCmd(t *testing.T) {
	for name, tc := range map[string]struct {
		resp   *mgmtpb.SystemStartResp
		msErr  error
		expErr error
	}{
		"system start duplicate rank results": {
			resp: &mgmtpb.SystemStartResp{
				Results: []*sharedpb.RankResult{
					{
						Rank: 2, Action: "start",
						State: system.MemberStateReady.String(),
					},
					{
						Rank: 2, Action: "start",
						Errored: true, Msg: "uh oh",
						State: system.MemberStateErrored.String(),
					},
				},
			},
			expErr: errors.New("duplicate result for rank"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("10.0.0.1:10001",
					tc.msErr, tc.resp),
			})

			startCmd := new(systemStartCmd)
			startCmd.setInvoker(mi)
			startCmd.SetLog(log)

			gotErr := startCmd.Execute(nil)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestDmg_systemRebuildOpCmd_execute(t *testing.T) {
	for name, tc := range map[string]struct {
		ctlCfg  *control.Config
		opCode  control.PoolRebuildOpCode
		force   bool
		verbose bool
		resp    *mgmtpb.SystemRebuildManageResp
		msErr   error
		expErr  error
		expInfo string
	}{
		"no config": {
			opCode: control.PoolRebuildOpCodeStop,
			expErr: errors.New("system rebuild stop failed: no configuration loaded"),
		},
		"ms failures": {
			ctlCfg: &control.Config{},
			resp:   &mgmtpb.SystemRebuildManageResp{},
			msErr:  errors.New("failed"),
			expErr: errors.New("failed"),
		},
		"no pools": {
			ctlCfg: &control.Config{},
			opCode: control.PoolRebuildOpCodeStop,
			resp:   &mgmtpb.SystemRebuildManageResp{},
			expInfo: `No pools in system.
Command completed successfully.
`,
		},
		"no pools; verbose": {
			ctlCfg:  &control.Config{},
			opCode:  control.PoolRebuildOpCodeStart,
			verbose: true,
			resp:    &mgmtpb.SystemRebuildManageResp{},
			expInfo: `No pools in system.
Command completed successfully.
`,
		},
		"rebuild stop with DER_NONEXIST only": {
			ctlCfg: &control.Config{},
			opCode: control.PoolRebuildOpCodeStop,
			resp: &mgmtpb.SystemRebuildManageResp{
				Results: []*mgmtpb.PoolRebuildManageResult{
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool1",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool2",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
				},
			},
			expInfo: `System-rebuild stop requested for 2 pools
   Without active rebuild: 2 pools
   Errors:                 0 pools
Command completed successfully.
`,
		},
		"rebuild stop mixed success and DER_NONEXIST": {
			ctlCfg: &control.Config{},
			opCode: control.PoolRebuildOpCodeStop,
			resp: &mgmtpb.SystemRebuildManageResp{
				Results: []*mgmtpb.PoolRebuildManageResult{
					{
						Id:     "pool_success1",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Id:     "pool_success2",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool_notrebuilding1",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool_notrebuilding2",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
				},
			},
			expInfo: `System-rebuild stop requested for 4 pools
   With active rebuild:    2 pools
   Without active rebuild: 2 pools
   Errors:                 0 pools
Command completed successfully.
`,
		},
		"rebuild stop with DER_NONEXIST and real errors": {
			ctlCfg: &control.Config{},
			opCode: control.PoolRebuildOpCodeStop,
			resp: &mgmtpb.SystemRebuildManageResp{
				Results: []*mgmtpb.PoolRebuildManageResult{
					{
						Id:     "pool_success",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool_notrebuilding",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "real error happened",
						Id:      "pool_failed",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
				},
			},
			expErr: errors.New("pool-rebuild stop failed on pool pool_failed: real error happened"),
			expInfo: `System-rebuild stop requested for 3 pools
   With active rebuild:    1 pool
   Without active rebuild: 1 pool
   Errors:                 1 pool
`,
		},
		"rebuild stop failed": {
			ctlCfg: &control.Config{},
			opCode: control.PoolRebuildOpCodeStop,
			resp: &mgmtpb.SystemRebuildManageResp{
				Results: []*mgmtpb.PoolRebuildManageResult{
					{
						Errored: true,
						Msg:     "failed",
						Id:      "foo",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "failed",
						Id:      "bar",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Id:     "baz",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
				},
			},
			expErr: errors.New("failed on pool foo: failed, pool-rebuild stop failed on pool bar"),
			expInfo: `System-rebuild stop requested for 3 pools
   With active rebuild:    1 pool
   Errors:                 2 pools
`,
		},
		"rebuild start succeeded; verbose": {
			ctlCfg:  &control.Config{},
			opCode:  control.PoolRebuildOpCodeStart,
			verbose: true,
			resp: &mgmtpb.SystemRebuildManageResp{
				Results: []*mgmtpb.PoolRebuildManageResult{
					{
						Id:     "foo",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Id:     "bar",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Id:     "baz",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
				},
			},
			expInfo: `System-rebuild start requested for 3 pools
   With active rebuild:    3 pools (foo, bar, baz)
   Errors:                 0 pools
Command completed successfully.
`,
		},
		"rebuild stop with errors; verbose": {
			ctlCfg:  &control.Config{},
			opCode:  control.PoolRebuildOpCodeStop,
			verbose: true,
			resp: &mgmtpb.SystemRebuildManageResp{
				Results: []*mgmtpb.PoolRebuildManageResult{
					{
						Id:     "pool_ok1",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Id:     "pool_ok2",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool_norebuild",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "error1",
						Id:      "pool_err1",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "error2",
						Id:      "pool_err2",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
				},
			},
			expErr: errors.New("error1, pool-rebuild stop failed on pool pool_err2: error2"),
			expInfo: `System-rebuild stop requested for 5 pools
   With active rebuild:    2 pools (pool_ok1, pool_ok2)
   Without active rebuild: 1 pool (pool_norebuild)
   Errors:                 2 pools (pool_err1, pool_err2)
`,
		},
		"rebuild stop with only DER_NONEXIST; verbose": {
			ctlCfg:  &control.Config{},
			opCode:  control.PoolRebuildOpCodeStop,
			verbose: true,
			resp: &mgmtpb.SystemRebuildManageResp{
				Results: []*mgmtpb.PoolRebuildManageResult{
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "uuid1",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "uuid2",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "uuid3",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
				},
			},
			expInfo: `System-rebuild stop requested for 3 pools
   Without active rebuild: 3 pools (uuid1, uuid2, uuid3)
   Errors:                 0 pools
Command completed successfully.
`,
		},
		"rebuild stop mixed categories; verbose": {
			ctlCfg:  &control.Config{},
			opCode:  control.PoolRebuildOpCodeStop,
			verbose: true,
			resp: &mgmtpb.SystemRebuildManageResp{
				Results: []*mgmtpb.PoolRebuildManageResult{
					{
						Id:     "pool1",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Id:     "pool2",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Id:     "pool3",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Id:     "pool4",
						OpCode: uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool5",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool6",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool7",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "DER_NONEXIST(-1005): The specified entity does not exist",
						Id:      "pool8",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "actual error 1",
						Id:      "pool9",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
					{
						Errored: true,
						Msg:     "actual error 2",
						Id:      "pool10",
						OpCode:  uint32(control.PoolRebuildOpCodeStop),
					},
				},
			},
			expErr: errors.New("actual error 1, pool-rebuild stop failed on pool pool10: actual error 2"),
			expInfo: `System-rebuild stop requested for 10 pools
   With active rebuild:    4 pools (pool1, pool2, pool3, pool4)
   Without active rebuild: 4 pools (pool5, pool6, pool7, pool8)
   Errors:                 2 pools (pool9, pool10)
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("10.0.0.1:10001",
					tc.msErr, tc.resp),
			})

			rbldCmd := new(systemRebuildOpCmd)
			rbldCmd.setInvoker(mi)
			rbldCmd.SetLog(log)
			rbldCmd.setConfig(tc.ctlCfg)
			rbldCmd.Verbose = tc.verbose

			gotErr := rbldCmd.execute(tc.opCode, tc.force)
			test.CmpErr(t, tc.expErr, gotErr)

			if tc.expInfo == "" {
				if strings.Contains(buf.String(), "INFO") {
					t.Fatalf("unexpected INFO log output printed, got:\n%s", buf.String())
				}
				return
			}

			gotInfo := extractInfoMessages(buf.String())
			if gotInfo != tc.expInfo {
				t.Fatalf("INFO output mismatch:\nexpected:\n%s\ngot:\n%s\nfull buffer:\n%s",
					tc.expInfo, gotInfo, buf.String())
			}
		})
	}
}

func TestDmg_systemSelfHealEvalCmd_execute(t *testing.T) {
	for name, tc := range map[string]struct {
		ctlCfg  *control.Config
		resp    *mgmtpb.DaosResp
		msErr   error
		expErr  error
		expInfo string
	}{
		"no config": {
			expErr: errors.New("system self-heal eval failed: no configuration loaded"),
		},
		"ms failures": {
			ctlCfg: &control.Config{},
			msErr:  errors.New("failed"),
			expErr: errors.New("failed"),
		},
		"success": {
			ctlCfg:  &control.Config{},
			resp:    &mgmtpb.DaosResp{},
			expInfo: "System self-heal eval request succeeded",
		},
		"daos error": {
			ctlCfg: &control.Config{},
			resp: &mgmtpb.DaosResp{
				Status: -1,
			},
			expErr: errors.New("DER_UNKNOWN"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("10.0.0.1:10001",
					tc.msErr, tc.resp),
			})

			cmd := new(systemSelfHealEvalCmd)
			cmd.setInvoker(mi)
			cmd.SetLog(log)
			cmd.setConfig(tc.ctlCfg)

			gotErr := cmd.Execute(nil)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
