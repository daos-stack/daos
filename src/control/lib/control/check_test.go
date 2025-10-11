//
// (C) Copyright 2023 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
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
