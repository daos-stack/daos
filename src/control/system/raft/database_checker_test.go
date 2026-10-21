//
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system/checker"
	"github.com/pkg/errors"
)

func TestRaft_Database_SetCheckerFindingAction(t *testing.T) {
	createDBWithFindings := func(t *testing.T, log logging.Logger, findings ...*checker.Finding) *Database {
		db := MockDatabase(t, log)
		for _, f := range findings {
			if err := db.data.Checker.addFinding(f); err != nil {
				t.Fatal(err)
			}
		}
		return db
	}

	staleMsg := checker.GetActionMsg(0, chk.CheckInconsistAction_CIA_STALE)

	for name, tc := range map[string]struct {
		startFindings    []*checker.Finding
		seq              uint64
		action           chk.CheckInconsistAction
		expErr           error
		expActionChoices []chk.CheckInconsistAction
		expActionMsg     []string
		expActionDetails []string
	}{
		"invalid action": {
			action: chk.CheckInconsistAction(4242), // arbitrary
			expErr: errors.New("invalid action"),
		},
		"empty db": {
			seq:    123,
			action: chk.CheckInconsistAction_CIA_IGNORE,
			expErr: ErrFindingNotFound(123),
		},
		"not found": {
			startFindings: []*checker.Finding{
				{CheckReport: chk.CheckReport{Seq: 100}},
				{CheckReport: chk.CheckReport{Seq: 101}},
				{CheckReport: chk.CheckReport{Seq: 102}},
			},
			seq:    123,
			action: chk.CheckInconsistAction_CIA_IGNORE,
			expErr: ErrFindingNotFound(123),
		},
		"stale, no action choices": {
			startFindings: []*checker.Finding{
				{CheckReport: chk.CheckReport{Seq: 100}},
				{CheckReport: chk.CheckReport{Seq: 101}},
				{CheckReport: chk.CheckReport{Seq: 102}},
			},
			seq:    101,
			action: chk.CheckInconsistAction_CIA_STALE,
			expActionMsg: []string{
				staleMsg,
			},
		},
		"stale ignores choices": {
			startFindings: []*checker.Finding{
				{
					CheckReport: chk.CheckReport{
						Seq: 101,
						ActChoices: []chk.CheckInconsistAction{
							chk.CheckInconsistAction_CIA_IGNORE,
							chk.CheckInconsistAction_CIA_TRUST_MS,
							chk.CheckInconsistAction_CIA_TRUST_PS,
						},
						ActMsgs: []string{
							"one",
							"two",
							"three",
						},
						ActDetails: []string{
							"detail1",
							"detail2",
							"detail3",
						},
					},
				},
			},
			seq:              101,
			action:           chk.CheckInconsistAction_CIA_STALE,
			expActionChoices: nil, // cleared
			expActionDetails: nil, // cleared
			expActionMsg: []string{
				staleMsg,
			},
		},
		"valid choice": {
			startFindings: []*checker.Finding{
				{
					CheckReport: chk.CheckReport{
						Seq: 101,
						ActChoices: []chk.CheckInconsistAction{
							chk.CheckInconsistAction_CIA_IGNORE,
							chk.CheckInconsistAction_CIA_TRUST_MS,
							chk.CheckInconsistAction_CIA_TRUST_PS,
						},
						ActMsgs: []string{
							"one",
							"two",
							"three",
						},
						ActDetails: []string{
							"detail1",
							"detail2",
							"detail3",
						},
					},
				},
			},
			seq:              101,
			action:           chk.CheckInconsistAction_CIA_TRUST_MS,
			expActionChoices: nil, // cleared
			expActionMsg: []string{
				"two",
			},
			expActionDetails: []string{
				"detail2",
			},
		},
		"no messages or details": {
			startFindings: []*checker.Finding{
				{
					CheckReport: chk.CheckReport{
						Seq: 101,
						ActChoices: []chk.CheckInconsistAction{
							chk.CheckInconsistAction_CIA_IGNORE,
							chk.CheckInconsistAction_CIA_TRUST_MS,
							chk.CheckInconsistAction_CIA_TRUST_PS,
						},
					},
				},
			},
			seq:              101,
			action:           chk.CheckInconsistAction_CIA_TRUST_MS,
			expActionChoices: nil, // cleared
		},
		"unavailable choice": {
			startFindings: []*checker.Finding{
				{
					CheckReport: chk.CheckReport{
						Seq: 101,
						ActChoices: []chk.CheckInconsistAction{
							chk.CheckInconsistAction_CIA_IGNORE,
							chk.CheckInconsistAction_CIA_TRUST_MS,
							chk.CheckInconsistAction_CIA_TRUST_PS,
						},
						ActMsgs: []string{
							"one",
							"two",
							"three",
						},
						ActDetails: []string{
							"detail1",
							"detail2",
							"detail3",
						},
					},
				},
			},
			seq:    101,
			action: chk.CheckInconsistAction_CIA_TRUST_EC_DATA,
			expErr: errors.New("action not available"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			db := createDBWithFindings(t, logging.FromContext(ctx), tc.startFindings...)

			err := db.SetCheckerFindingAction(tc.seq, int32(tc.action))

			test.CmpErr(t, tc.expErr, err)

			if tc.expErr == nil {
				// Check that the action was actually updated
				f, err := db.GetCheckerFinding(tc.seq)
				if err != nil {
					t.Fatal(err)
				}

				test.AssertEqual(t, tc.action, f.Action, "verifying action was set")
				test.CmpAny(t, "action choices", tc.expActionChoices, f.ActChoices)
				test.CmpAny(t, "action messages", tc.expActionMsg, f.ActMsgs)
				test.CmpAny(t, "action details", tc.expActionDetails, f.ActDetails)
			}
		})
	}
}
