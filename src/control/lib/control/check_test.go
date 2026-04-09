//
// (C) Copyright 2023 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/protobuf/testing/protocmp"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
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
