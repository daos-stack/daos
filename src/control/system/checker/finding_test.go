//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker_test

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/common"
	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/system/checker"
)

func TestChecker_AnnotateFinding(t *testing.T) {
	for name, tc := range map[string]struct {
		rpt        *chkpb.CheckReport
		expFinding *checker.Finding
	}{
		"nil report": {},
		// Ideally each report should have a list of details to match the
		// list of actions. However, the list of details is not required
		// and we want to verify that the code handles this correctly.
		"pad details to match actions": {
			rpt: &chkpb.CheckReport{
				Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
				Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
				PoolUuid: common.MockUUID(),
				Actions: []chkpb.CheckInconsistAction{
					chkpb.CheckInconsistAction_CIA_TRUST_MS,
					chkpb.CheckInconsistAction_CIA_TRUST_PS,
					chkpb.CheckInconsistAction_CIA_IGNORE,
				},
				Details: []string{"ms-label", "ps-label"},
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
					Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
					PoolUuid: common.MockUUID(),
					Actions: []chkpb.CheckInconsistAction{
						chkpb.CheckInconsistAction_CIA_TRUST_MS,
						chkpb.CheckInconsistAction_CIA_TRUST_PS,
						chkpb.CheckInconsistAction_CIA_IGNORE,
					},
					Msg: fmt.Sprintf("The pool label for %s does not match MS", common.MockUUID()),
					Details: []string{
						fmt.Sprintf("Trust the MS pool entry (ms-label) for %s", common.MockUUID()),
						fmt.Sprintf("Trust the PS pool entry (ps-label) for %s", common.MockUUID()),
						"Ignore the pool finding",
					},
				}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := checker.NewFinding(tc.rpt)

			gotFinding := checker.AnnotateFinding(f)
			if tc.expFinding == nil && gotFinding == nil {
				return
			}

			if diff := cmp.Diff(tc.expFinding, gotFinding, protocmp.Transform()); diff != "" {
				t.Fatalf("unexpected finding (-want +got):\n%s", diff)
			}
		})
	}
}
