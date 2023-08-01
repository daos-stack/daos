//
// (C) Copyright 2019-2023 Intel Corporation.
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

func TestDmg_LeaderQueryCmd_Errors(t *testing.T) {
	for name, tc := range map[string]struct {
		ctlCfg *control.Config
		resp   *mgmtpb.LeaderQueryResp
		msErr  error
		expErr error
	}{
		"list pools no config": {
			resp:   &mgmtpb.LeaderQueryResp{},
			expErr: errors.New("leader query failed: no configuration loaded"),
		},
		"list pools success": {
			ctlCfg: &control.Config{},
			resp:   &mgmtpb.LeaderQueryResp{},
		},
		"list pools ms failures": {
			ctlCfg: &control.Config{},
			resp:   &mgmtpb.LeaderQueryResp{},
			msErr:  errors.New("remote failed"),
			expErr: errors.New("remote failed"),
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

func TestDmg_systemQueryCmd_Errors(t *testing.T) {
	for name, tc := range map[string]struct {
		resp   *mgmtpb.SystemQueryResp
		msErr  error
		expErr error
	}{
		"system query success": {
			resp: &mgmtpb.SystemQueryResp{
				Members: []*mgmtpb.SystemMember{
					{
						Rank:  1,
						Uuid:  test.MockUUID(1),
						State: system.MemberStateReady.String(),
						Addr:  "10.0.0.1:10001",
					},
					{
						Rank:  2,
						Uuid:  test.MockUUID(2),
						State: system.MemberStateReady.String(),
						Addr:  "10.0.0.1:10001",
					},
				},
			},
		},
		"system query ms failures": {
			msErr:  errors.New("remote failed"),
			expErr: errors.New("remote failed"),
		},
		"system query absent hosts": {
			resp: &mgmtpb.SystemQueryResp{
				Absenthosts: "foo[1-23]",
			},
			expErr: errors.New("system query failed: non-existent hosts"),
		},
		"system query absent ranks": {
			resp: &mgmtpb.SystemQueryResp{
				Absentranks: "1-23",
			},
			expErr: errors.New("system query failed: non-existent ranks"),
		},
		"system query absent hosts and ranks": {
			resp: &mgmtpb.SystemQueryResp{
				Absenthosts: "foo[1-23]",
				Absentranks: "1-23",
			},
			expErr: errors.New("system query failed: non-existent hosts foo[1-23], " +
				"non-existent ranks 1-23"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("10.0.0.1:10001",
					tc.msErr, tc.resp),
			})

			queryCmd := new(systemQueryCmd)
			queryCmd.setInvoker(mi)
			queryCmd.SetLog(log)

			gotErr := queryCmd.Execute(nil)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestDmg_systemStartCmd_Errors(t *testing.T) {
	for name, tc := range map[string]struct {
		resp   *mgmtpb.SystemStartResp
		msErr  error
		expErr error
	}{
		"system start success": {
			resp: &mgmtpb.SystemStartResp{
				Results: []*sharedpb.RankResult{
					{
						Rank: 2, Action: "start",
						State: system.MemberStateReady.String(),
					},
					{
						Rank: 3, Action: "start",
						State: system.MemberStateReady.String(),
					},
				},
			},
		},
		"system start ms failures": {
			resp: &mgmtpb.SystemStartResp{
				Absenthosts: "foo[1-23]",
				Absentranks: "1-23",
				Results: []*sharedpb.RankResult{
					{
						Rank: 24, Action: "start",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
				},
			},
			msErr:  errors.New("remote failed"),
			expErr: errors.New("remote failed"),
		},
		"system start rank failures": {
			resp: &mgmtpb.SystemStartResp{
				Results: []*sharedpb.RankResult{
					{
						Rank: 2, Action: "start",
						State: system.MemberStateReady.String(),
					},
					{
						Rank: 3, Action: "start",
						Errored: true, Msg: "uh oh",
						State: system.MemberStateErrored.String(),
					},
					{
						Rank: 5, Action: "start",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
					{
						Rank: 4, Action: "start",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
				},
			},
			expErr: errors.New("system start failed: check results for failed ranks 3-5"),
		},
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
			expErr: errors.New("system start failed: duplicate result"),
		},
		"system start absent hosts": {
			resp: &mgmtpb.SystemStartResp{
				Absenthosts: "foo[1-23]",
			},
			expErr: errors.New("system start failed: non-existent hosts"),
		},
		"system start absent ranks": {
			resp: &mgmtpb.SystemStartResp{
				Absentranks: "1-23",
			},
			expErr: errors.New("system start failed: non-existent ranks"),
		},
		"system start absent hosts and ranks and failed ranks": {
			resp: &mgmtpb.SystemStartResp{
				Absenthosts: "foo[1-23]",
				Absentranks: "1-23",
				Results: []*sharedpb.RankResult{
					{
						Rank: 24, Action: "start",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
					{
						Rank: 25, Action: "start",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
				},
			},
			expErr: errors.New("system start failed: non-existent hosts foo[1-23], " +
				"non-existent ranks 1-23, check results for failed ranks 24-2"),
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

func TestDmg_systemStopCmd_Errors(t *testing.T) {
	for name, tc := range map[string]struct {
		resp   *mgmtpb.SystemStopResp
		msErr  error
		expErr error
	}{
		"system stop success": {
			resp: &mgmtpb.SystemStopResp{
				Results: []*sharedpb.RankResult{
					{
						Rank: 2, Action: "stop",
						State: system.MemberStateStopped.String(),
					},
					{
						Rank: 3, Action: "stop",
						State: system.MemberStateStopped.String(),
					},
				},
			},
		},
		"system stop ms failures": {
			resp: &mgmtpb.SystemStopResp{
				Absenthosts: "foo[1-23]",
				Absentranks: "1-23",
				Results: []*sharedpb.RankResult{
					{
						Rank: 24, Action: "stop",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
				},
			},
			msErr:  errors.New("remote failed"),
			expErr: errors.New("remote failed"),
		},
		"system stop rank failures": {
			resp: &mgmtpb.SystemStopResp{
				Results: []*sharedpb.RankResult{
					{
						Rank: 2, Action: "stop",
						State: system.MemberStateStopped.String(),
					},
					{
						Rank: 3, Action: "stop",
						Errored: true, Msg: "uh oh",
						State: system.MemberStateErrored.String(),
					},
					{
						Rank: 5, Action: "stop",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
					{
						Rank: 4, Action: "stop",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
				},
			},
			expErr: errors.New("system stop failed: check results for failed ranks 3-5"),
		},
		"system stop duplicate rank results": {
			resp: &mgmtpb.SystemStopResp{
				Results: []*sharedpb.RankResult{
					{
						Rank: 2, Action: "stop",
						State: system.MemberStateStopped.String(),
					},
					{
						Rank: 2, Action: "stop",
						Errored: true, Msg: "uh oh",
						State: system.MemberStateErrored.String(),
					},
				},
			},
			expErr: errors.New("system stop failed: duplicate result"),
		},
		"system stop absent hosts": {
			resp: &mgmtpb.SystemStopResp{
				Absenthosts: "foo[1-23]",
			},
			expErr: errors.New("system stop failed: non-existent hosts"),
		},
		"system stop absent ranks": {
			resp: &mgmtpb.SystemStopResp{
				Absentranks: "1-23",
			},
			expErr: errors.New("system stop failed: non-existent ranks"),
		},
		"system stop absent hosts and ranks and failed ranks": {
			resp: &mgmtpb.SystemStopResp{
				Absenthosts: "foo[1-23]",
				Absentranks: "1-23",
				Results: []*sharedpb.RankResult{
					{
						Rank: 24, Action: "stop",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
					{
						Rank: 25, Action: "stop",
						Errored: true, Msg: "uh ohh",
						State: system.MemberStateErrored.String(),
					},
				},
			},
			expErr: errors.New("system stop failed: non-existent hosts foo[1-23], " +
				"non-existent ranks 1-23, check results for failed ranks 24-2"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("10.0.0.1:10001",
					tc.msErr, tc.resp),
			})

			stopCmd := new(systemStopCmd)
			stopCmd.setInvoker(mi)
			stopCmd.SetLog(log)

			gotErr := stopCmd.Execute(nil)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
