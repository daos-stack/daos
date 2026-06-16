//
// (C) Copyright 2023 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/testing/protocmp"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestControl_SystemCheckReport_RepairChoices(t *testing.T) {
	for name, tc := range map[string]struct {
		report     *SystemCheckReport
		expChoices []*SystemCheckRepairChoice
	}{
		"nil": {},
		"no choices": {
			report:     &SystemCheckReport{},
			expChoices: []*SystemCheckRepairChoice{},
		},
		"no details": {
			report: &SystemCheckReport{
				CheckReport: chkpb.CheckReport{
					ActChoices: []chkpb.CheckInconsistAction{chkpb.CheckInconsistAction_CIA_TRUST_MS},
					ActMsgs:    []string{"action message"},
					ActDetails: []string{""},
				},
			},
			expChoices: []*SystemCheckRepairChoice{
				{
					Action: SystemCheckRepairAction(chkpb.CheckInconsistAction_CIA_TRUST_MS),
					Info:   "action message",
				},
			},
		},
		"has details": {
			report: &SystemCheckReport{
				CheckReport: chkpb.CheckReport{
					ActChoices: []chkpb.CheckInconsistAction{chkpb.CheckInconsistAction_CIA_TRUST_MS},
					ActMsgs:    []string{"action message"},
					ActDetails: []string{"action details"},
				},
			},
			expChoices: []*SystemCheckRepairChoice{
				{
					Action: SystemCheckRepairAction(chkpb.CheckInconsistAction_CIA_TRUST_MS),
					Info:   "action details",
				},
			},
		},
		"same order": {
			report: &SystemCheckReport{
				CheckReport: chkpb.CheckReport{
					ActChoices: []chkpb.CheckInconsistAction{
						chkpb.CheckInconsistAction_CIA_TRUST_PS,
						chkpb.CheckInconsistAction_CIA_TRUST_MS,
						chkpb.CheckInconsistAction_CIA_TRUST_OLDEST,
						chkpb.CheckInconsistAction_CIA_TRUST_LATEST,
					},
					ActMsgs: []string{
						"trust PS",
						"trust MS",
						"trust oldest",
						"trust latest",
					},
					ActDetails: []string{"", "", "", ""},
				},
			},
			expChoices: []*SystemCheckRepairChoice{
				{
					Action: SystemCheckRepairAction(chkpb.CheckInconsistAction_CIA_TRUST_PS),
					Info:   "trust PS",
				},
				{
					Action: SystemCheckRepairAction(chkpb.CheckInconsistAction_CIA_TRUST_MS),
					Info:   "trust MS",
				},
				{
					Action: SystemCheckRepairAction(chkpb.CheckInconsistAction_CIA_TRUST_OLDEST),
					Info:   "trust oldest",
				},
				{
					Action: SystemCheckRepairAction(chkpb.CheckInconsistAction_CIA_TRUST_LATEST),
					Info:   "trust latest",
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.report.RepairChoices()

			if diff := cmp.Diff(tc.expChoices, result); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}
		})
	}
}

func TestControl_SystemCheckReport_IsDryRun(t *testing.T) {
	for name, tc := range map[string]struct {
		report    *SystemCheckReport
		expResult bool
	}{
		"nil": {},
		"success result": {
			report: &SystemCheckReport{
				chkpb.CheckReport{
					Result: int32(chkpb.CheckResult_SUCCESS),
				},
			},
		},
		"error result": {
			report: &SystemCheckReport{
				chkpb.CheckReport{
					Result: int32(daos.MiscError),
				},
			},
		},
		"unknown positive result": {
			report: &SystemCheckReport{
				chkpb.CheckReport{
					Result: 1000,
				},
			},
		},
		"dry run result": {
			report: &SystemCheckReport{
				chkpb.CheckReport{
					Result: int32(chkpb.CheckResult_DRY_RUN),
				},
			},
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.report.IsDryRun(), "")
		})
	}
}

func TestControl_SystemCheckReport_IsInteractive(t *testing.T) {
	expInteractive := chkpb.CheckInconsistAction_CIA_INTERACT

	for name, actVal := range chkpb.CheckInconsistAction_value {
		t.Run(name, func(t *testing.T) {
			action := chkpb.CheckInconsistAction(actVal)
			report := &SystemCheckReport{
				chkpb.CheckReport{
					Action: action,
				},
			}

			test.AssertEqual(t, action == expInteractive, report.IsInteractive(), "")
		})
	}
}

func TestControl_SystemCheckReport_IsStale(t *testing.T) {
	expStaleAction := chkpb.CheckInconsistAction_CIA_STALE

	for name, actVal := range chkpb.CheckInconsistAction_value {
		t.Run(name, func(t *testing.T) {
			action := chkpb.CheckInconsistAction(actVal)
			report := &SystemCheckReport{
				chkpb.CheckReport{
					Action: action,
				},
			}

			test.AssertEqual(t, action == expStaleAction, report.IsStale(), "")
		})
	}
}

func TestControl_SystemCheckQuery(t *testing.T) {
	testTime := time.Now()

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *SystemCheckQueryReq
		expErr  error
		expResp *SystemCheckQueryResp
	}{
		"nil req": {
			expErr: errors.New("nil"),
		},
		"gRPC failed": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("foobar"),
			},
			req:    &SystemCheckQueryReq{},
			expErr: errors.New("foobar"),
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("", nil, &mgmtpb.CheckQueryResp{
					ReqStatus: daos.MiscError.Int32(),
					InsStatus: chkpb.CheckInstStatus_CIS_RUNNING,
					InsPhase:  chkpb.CheckScanPhase_CSP_CONT_CLEANUP,
					Inconsistency: &mgmtpb.CheckQueryInconsist{
						Total:    5,
						Repaired: 3,
						Ignored:  1,
						Failed:   1,
					},
					Time: &mgmtpb.CheckQueryTime{StartTime: uint64(testTime.Unix())},
					Pools: []*mgmtpb.CheckQueryPool{
						{
							Uuid:          "12345678-1234-1234-1234-123456789abc",
							Status:        chkpb.CheckPoolStatus_CPS_CHECKED,
							Phase:         chkpb.CheckScanPhase_CSP_DONE,
							Inconsistency: &mgmtpb.CheckQueryInconsist{},
							Time:          &mgmtpb.CheckQueryTime{StartTime: uint64(testTime.Unix())},
							Targets: []*mgmtpb.CheckQueryTarget{
								{
									Rank:          3,
									Status:        chkpb.CheckInstStatus_CIS_COMPLETED,
									Inconsistency: &mgmtpb.CheckQueryInconsist{},
									Time:          &mgmtpb.CheckQueryTime{StartTime: uint64(testTime.Unix())},
								},
							},
						},
					},
					Reports: []*chkpb.CheckReport{
						{Seq: 1, Class: chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL},
						{Seq: 3, Class: chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL},
					},
					Leader: 5,
				}),
			},
			req: &SystemCheckQueryReq{},
			expResp: &SystemCheckQueryResp{
				Status:    SystemCheckStatusRunning,
				ScanPhase: SystemCheckScanPhaseContainerCleanup,
				StartTime: time.Unix(testTime.Unix(), 0),
				Leader:    5,
				Pools: map[string]*SystemCheckPoolInfo{
					"12345678-1234-1234-1234-123456789abc": {
						RawRankInfo: rawRankMap{
							3: {
								Uuid:          "12345678-1234-1234-1234-123456789abc",
								Status:        chkpb.CheckPoolStatus_CPS_CHECKED,
								Phase:         chkpb.CheckScanPhase_CSP_DONE,
								Inconsistency: &mgmtpb.CheckQueryInconsist{},
								Time:          &mgmtpb.CheckQueryTime{StartTime: uint64(testTime.Unix())},
								Targets: []*mgmtpb.CheckQueryTarget{
									{
										Rank:          3,
										Status:        chkpb.CheckInstStatus_CIS_COMPLETED,
										Inconsistency: &mgmtpb.CheckQueryInconsist{},
										Time:          &mgmtpb.CheckQueryTime{StartTime: uint64(testTime.Unix())},
									},
								},
							},
						},
						UUID:      "12345678-1234-1234-1234-123456789abc",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: time.Unix(testTime.Unix(), 0),
					},
				},
				Reports: []*SystemCheckReport{
					{CheckReport: chkpb.CheckReport{Seq: 1, Class: chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL}},
					{CheckReport: chkpb.CheckReport{Seq: 3, Class: chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL}},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			mi := NewMockInvoker(logging.FromContext(ctx), tc.mic)

			resp, err := SystemCheckQuery(ctx, mi, tc.req)

			test.CmpErr(t, tc.expErr, err)
			test.CmpAny(t, "SystemCheckQueryResp", tc.expResp, resp, cmpopts.IgnoreUnexported(
				chkpb.CheckReport{},
				mgmtpb.CheckQueryPool{},
				mgmtpb.CheckQueryInconsist{},
				mgmtpb.CheckQueryTarget{},
				mgmtpb.CheckQueryTime{},
			))
		})
	}
}

func TestControl_SystemCheckQuery_ReportsSorted(t *testing.T) {
	// Reports are returned in scrambled order to verify that
	// SystemCheckQuery sorts them by class, then by sequence.
	mockResp := &mgmtpb.CheckQueryResp{
		Reports: []*chkpb.CheckReport{
			{Seq: 3, Class: chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL},
			{Seq: 1, Class: chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL},
			{Seq: 5, Class: chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS},
			{Seq: 4, Class: chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS},
			{Seq: 2, Class: chkpb.CheckInconsistClass_CIC_POOL_MORE_SVC},
		},
	}

	mi := NewMockInvoker(nil, &MockInvokerConfig{
		UnaryResponse: MockMSResponse("", nil, mockResp),
	})

	resp, err := SystemCheckQuery(test.Context(t), mi, &SystemCheckQueryReq{})
	if err != nil {
		t.Fatal(err)
	}

	expReports := []*SystemCheckReport{
		{chkpb.CheckReport{Seq: 2, Class: chkpb.CheckInconsistClass_CIC_POOL_MORE_SVC}},
		{chkpb.CheckReport{Seq: 5, Class: chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS}},
		{chkpb.CheckReport{Seq: 1, Class: chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL}},
		{chkpb.CheckReport{Seq: 3, Class: chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL}},
		{chkpb.CheckReport{Seq: 4, Class: chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS}},
	}

	if diff := cmp.Diff(expReports, resp.Reports, protocmp.Transform()); diff != "" {
		t.Fatalf("reports not sorted (-want +got):\n%s", diff)
	}
}

func TestControl_SystemCheckEngineReport(t *testing.T) {
	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *SystemCheckEngineReportReq
		expErr  error
		expResp *SystemCheckEngineReportResp
	}{
		"nil req": {
			expErr: errors.New("nil"),
		},
		"nil report": {
			req:    &SystemCheckEngineReportReq{},
			expErr: errors.New("no check report"),
		},
		"gRPC fails": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("MockInvoker error"),
			},
			req: &SystemCheckEngineReportReq{
				CheckReportReq: sharedpb.CheckReportReq{
					Report: &chkpb.CheckReport{},
				},
			},
			expErr: errors.New("MockInvoker error"),
		},
		"MS error": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Error: errors.New("MockInvoker response error"),
						},
					},
				},
			},
			req: &SystemCheckEngineReportReq{
				CheckReportReq: sharedpb.CheckReportReq{
					Report: &chkpb.CheckReport{},
				},
			},
			expErr: errors.New("MockInvoker response error"),
		},
		"daos error code": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{{
						Message: &sharedpb.CheckReportResp{
							Status: daos.MiscError.Int32(),
						},
					}},
				},
			},
			req: &SystemCheckEngineReportReq{
				CheckReportReq: sharedpb.CheckReportReq{
					Report: &chkpb.CheckReport{},
				},
			},
			expResp: &SystemCheckEngineReportResp{
				CheckReportResp: sharedpb.CheckReportResp{
					Status: daos.MiscError.Int32(),
				},
			},
		},
		"bad MS response type": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Message: &mgmtpb.CheckStartReq{
								Sys: "something",
							},
						},
					},
				},
			},
			req: &SystemCheckEngineReportReq{
				CheckReportReq: sharedpb.CheckReportReq{
					Report: &chkpb.CheckReport{},
				},
			},
			expErr: errors.New("unexpected response"),
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{{
						Message: &sharedpb.CheckReportResp{},
					}},
				},
			},
			req: &SystemCheckEngineReportReq{
				CheckReportReq: sharedpb.CheckReportReq{
					Report: &chkpb.CheckReport{},
				},
			},
			expResp: &SystemCheckEngineReportResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			invoker := NewMockInvoker(logging.FromContext(ctx), tc.mic)
			resp, err := SystemCheckEngineReport(ctx, invoker, tc.req)

			test.CmpErr(t, tc.expErr, err)
			test.CmpAny(t, "response", tc.expResp, resp, cmpopts.IgnoreUnexported(sharedpb.CheckReportResp{}))
		})
	}
}

func TestControl_CheckEngineRepair(t *testing.T) {
	reqWithHostList := func(rank uint32, actReq *mgmtpb.CheckActReq, hosts ...string) *CheckEngineRepairReq {
		req := &CheckEngineRepairReq{
			CheckEngineActReq: ctlpb.CheckEngineActReq{
				Rank: rank,
				Req:  actReq,
			},
		}
		req.SetHostList(hosts)
		return req
	}

	defaultReq := reqWithHostList(42, &mgmtpb.CheckActReq{}, "1.2.3.4")

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *CheckEngineRepairReq
		expResp *CheckEngineRepairResp
		expErr  error
	}{
		"nil req": {
			expErr: errors.New("nil"),
		},
		"no action req": {
			req:    reqWithHostList(5, nil),
			expErr: errors.New("no action"),
		},
		"host list too long": {
			req:    reqWithHostList(5, &mgmtpb.CheckActReq{}, "1.2.3.4", "5.6.7.8"),
			expErr: errors.New("exactly one host"),
		},
		"gRPC fails": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("MockInvoker error"),
			},
			req:    defaultReq,
			expErr: errors.New("MockInvoker error"),
		},
		"no host response": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{},
				},
			},
			req:    defaultReq,
			expErr: errors.New("no host responses"),
		},
		"bad return type": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Message: &MockMessage{},
						},
					},
				},
			},
			req:    defaultReq,
			expErr: errors.New("bad response type"),
		},
		"got response": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Message: &ctlpb.CheckEngineActResp{
								Rank: 5,
								Resp: &mgmtpb.CheckActResp{
									Status: daos.MiscError.Int32(),
								},
							},
						},
					},
				},
			},
			req: defaultReq,
			expResp: &CheckEngineRepairResp{
				CheckEngineActResp: ctlpb.CheckEngineActResp{
					Rank: 5,
					Resp: &mgmtpb.CheckActResp{
						Status: daos.MiscError.Int32(),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			invoker := NewMockInvoker(logging.FromContext(ctx), tc.mic)
			resp, err := CheckEngineRepair(ctx, invoker, tc.req)

			test.CmpErr(t, tc.expErr, err)
			test.CmpAny(t, "response", tc.expResp, resp,
				cmpopts.IgnoreUnexported(ctlpb.CheckEngineActResp{}, mgmtpb.CheckActResp{}))
		})
	}
}
