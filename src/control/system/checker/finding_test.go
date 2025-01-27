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

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/common/test"
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
				Seq:      972946141031694340,
				Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
				Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
				PoolUuid: test.MockUUID(),
				ActChoices: []chkpb.CheckInconsistAction{
					chkpb.CheckInconsistAction_CIA_TRUST_MS,
					chkpb.CheckInconsistAction_CIA_TRUST_PS,
					chkpb.CheckInconsistAction_CIA_IGNORE,
				},
				ActDetails: []string{"ms-label", "ps-label"},
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:      972946141031694340,
					Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
					Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
					PoolUuid: test.MockUUID(),
					ActChoices: []chkpb.CheckInconsistAction{
						chkpb.CheckInconsistAction_CIA_TRUST_MS,
						chkpb.CheckInconsistAction_CIA_TRUST_PS,
						chkpb.CheckInconsistAction_CIA_IGNORE,
					},
					ActDetails: []string{"ms-label", "ps-label", ""},
					Msg:        "Inconsistency found: CIC_POOL_BAD_LABEL (details: [ms-label ps-label ])",
					ActMsgs: []string{
						fmt.Sprintf("Reset the pool property using the MS label for %s", test.MockUUID()),
						fmt.Sprintf("Update the MS label to use the pool property value for %s", test.MockUUID()),
						"Ignore the pool finding",
					},
				}),
		},
		"action report: orphaned pool restored to MS": {
			rpt: &chkpb.CheckReport{
				Seq:       972946141031694340,
				Class:     chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS,
				Action:    chkpb.CheckInconsistAction_CIA_TRUST_PS,
				PoolUuid:  "28b265f6-a70b-412f-9559-8b6df06b7f7f",
				Msg:       "Check leader detects orphan pool\n",
				Timestamp: "Mon Dec  5 19:25:39 2022\n",
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972946141031694340,
					Class:     chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS,
					Action:    chkpb.CheckInconsistAction_CIA_TRUST_PS,
					PoolUuid:  "28b265f6-a70b-412f-9559-8b6df06b7f7f",
					Msg:       "Check leader detects orphan pool",
					Timestamp: "Mon Dec  5 19:25:39 2022",
					ActMsgs: []string{
						"Recreate the MS pool entry for 28b265f6-a70b-412f-9559-8b6df06b7f7f",
					},
				}),
		},
		"action report: orphaned pool removed on engines": {
			rpt: &chkpb.CheckReport{
				Seq:       972946141031694340,
				Class:     chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS,
				Action:    chkpb.CheckInconsistAction_CIA_TRUST_MS,
				PoolUuid:  "28b265f6-a70b-412f-9559-8b6df06b7f7f",
				Msg:       "Check leader detects orphan pool\n",
				Timestamp: "Mon Dec  5 19:25:39 2022\n",
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972946141031694340,
					Class:     chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS,
					Action:    chkpb.CheckInconsistAction_CIA_TRUST_MS,
					PoolUuid:  "28b265f6-a70b-412f-9559-8b6df06b7f7f",
					Msg:       "Check leader detects orphan pool",
					Timestamp: "Mon Dec  5 19:25:39 2022",
					ActMsgs: []string{
						"Reclaim the orphaned pool storage for 28b265f6-a70b-412f-9559-8b6df06b7f7f",
					},
				}),
		},
		"action report: dangling pool removed from MS": {
			rpt: &chkpb.CheckReport{
				Seq:       972946141031694340,
				Class:     chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE,
				Action:    chkpb.CheckInconsistAction_CIA_TRUST_PS,
				PoolUuid:  "40da737d-e47f-0000-ffff-ffff00000000",
				Timestamp: "Mon Dec  5 19:25:39 2022\n",
				Msg:       "Check leader detects dangling pool\n",
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972946141031694340,
					Class:     chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE,
					Action:    chkpb.CheckInconsistAction_CIA_TRUST_PS,
					PoolUuid:  "40da737d-e47f-0000-ffff-ffff00000000",
					Timestamp: "Mon Dec  5 19:25:39 2022",
					Msg:       "Check leader detects dangling pool",
					ActMsgs: []string{
						"Remove the MS pool entry for 40da737d-e47f-0000-ffff-ffff00000000",
					},
				}),
		},
		"action report: corrupt pool label (fixed on PS)": {
			rpt: &chkpb.CheckReport{
				Seq:       972946141031694340,
				Class:     chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
				Action:    chkpb.CheckInconsistAction_CIA_TRUST_MS,
				PoolUuid:  "6a8bbf20-fb86-416d-8045-c23fbce5048a",
				Timestamp: "Mon Dec  5 19:25:52 2022\n",
				Msg:       "Check engine detects corrupted pool label: one-fault (MS) vs one (PS).\n",
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972946141031694340,
					Class:     chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
					Action:    chkpb.CheckInconsistAction_CIA_TRUST_MS,
					PoolUuid:  "6a8bbf20-fb86-416d-8045-c23fbce5048a",
					Timestamp: "Mon Dec  5 19:25:52 2022",
					Msg:       "Check engine detects corrupted pool label: one-fault (MS) vs one (PS).",
					ActMsgs: []string{
						"Reset the pool property using the MS label for 6a8bbf20-fb86-416d-8045-c23fbce5048a",
					},
				}),
		},
		"action report: corrupt pool label (fixed on MS)": {
			rpt: &chkpb.CheckReport{
				Seq:       972946141031694340,
				Class:     chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
				Action:    chkpb.CheckInconsistAction_CIA_TRUST_PS,
				PoolUuid:  "6a8bbf20-fb86-416d-8045-c23fbce5048a",
				Timestamp: "Mon Dec  5 19:25:52 2022\n",
				Msg:       "Check engine detects corrupted pool label: one-fault (MS) vs one (PS).\n",
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972946141031694340,
					Class:     chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
					Action:    chkpb.CheckInconsistAction_CIA_TRUST_PS,
					PoolUuid:  "6a8bbf20-fb86-416d-8045-c23fbce5048a",
					Timestamp: "Mon Dec  5 19:25:52 2022",
					Msg:       "Check engine detects corrupted pool label: one-fault (MS) vs one (PS).",
					ActMsgs: []string{
						"Update the MS label to use the pool property value for 6a8bbf20-fb86-416d-8045-c23fbce5048a",
					},
				}),
		},
		"action report: container non-exist on PS (discard)": {
			rpt: &chkpb.CheckReport{
				Seq:       972946141031694340,
				Class:     chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
				Action:    chkpb.CheckInconsistAction_CIA_DISCARD,
				Rank:      1,
				PoolUuid:  "04077e64-085c-489f-bd6c-bc895710414f",
				ContUuid:  "44b03d6a-b7e0-431b-818b-24c6cf331181",
				Timestamp: "Mon Dec  5 19:25:48 2022\n",
				Msg:       "Check engine detects orphan container 04077e64-085c-489f-bd6c-bc895710414f/44b03d6a-b7e0-431b-818b-24c6cf331181\n",
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972946141031694340,
					Class:     chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
					Action:    chkpb.CheckInconsistAction_CIA_DISCARD,
					Rank:      1,
					PoolUuid:  "04077e64-085c-489f-bd6c-bc895710414f",
					ContUuid:  "44b03d6a-b7e0-431b-818b-24c6cf331181",
					Timestamp: "Mon Dec  5 19:25:48 2022",
					Msg:       "Check engine detects orphan container 04077e64-085c-489f-bd6c-bc895710414f/44b03d6a-b7e0-431b-818b-24c6cf331181",
					ActMsgs: []string{
						"Discard the container",
					},
				}),
		},
		"action report: container label mismatch (fixed on targets)": {
			rpt: &chkpb.CheckReport{
				Seq:       972946141031694341,
				Class:     chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
				Action:    chkpb.CheckInconsistAction_CIA_TRUST_PS,
				Rank:      1,
				PoolUuid:  "d48a9aa7-4341-446a-8125-bb7eab3781b3",
				ContUuid:  "00c841c8-4e01-4001-bd58-7de5ad602926",
				Timestamp: "Mon Dec  5 19:25:49 2022\n",
				Msg:       "Check engine detects inconsistent container label: new-label (CS) vs six-cont (property).\n",
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972946141031694341,
					Class:     chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
					Action:    chkpb.CheckInconsistAction_CIA_TRUST_PS,
					Rank:      1,
					PoolUuid:  "d48a9aa7-4341-446a-8125-bb7eab3781b3",
					ContUuid:  "00c841c8-4e01-4001-bd58-7de5ad602926",
					Timestamp: "Mon Dec  5 19:25:49 2022",
					Msg:       "Check engine detects inconsistent container label: new-label (CS) vs six-cont (property).",
					ActMsgs: []string{
						"Reset the container property using the PS label for 00c841c8-4e01-4001-bd58-7de5ad602926",
					},
				}),
		},
		"action report: container label mismatch (fixed on PS)": {
			rpt: &chkpb.CheckReport{
				Seq:       972946141031694341,
				Class:     chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
				Action:    chkpb.CheckInconsistAction_CIA_TRUST_TARGET,
				Rank:      1,
				PoolUuid:  "d48a9aa7-4341-446a-8125-bb7eab3781b3",
				ContUuid:  "00c841c8-4e01-4001-bd58-7de5ad602926",
				Timestamp: "Mon Dec  5 19:25:49 2022\n",
				Msg:       "Check engine detects inconsistent container label: new-label (CS) vs six-cont (property).\n",
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972946141031694341,
					Class:     chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
					Action:    chkpb.CheckInconsistAction_CIA_TRUST_TARGET,
					Rank:      1,
					PoolUuid:  "d48a9aa7-4341-446a-8125-bb7eab3781b3",
					ContUuid:  "00c841c8-4e01-4001-bd58-7de5ad602926",
					Timestamp: "Mon Dec  5 19:25:49 2022",
					Msg:       "Check engine detects inconsistent container label: new-label (CS) vs six-cont (property).",
					ActMsgs: []string{
						"Update the CS label to use the container property value for 00c841c8-4e01-4001-bd58-7de5ad602926",
					},
				}),
		},
		"interactive finding: orphaned MS entry for pool": {
			rpt: &chkpb.CheckReport{
				Seq:       973024752426024961,
				Class:     chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE,
				Action:    chkpb.CheckInconsistAction_CIA_INTERACT,
				PoolUuid:  "e0f1371c-087f-0000-ffff-ffff00000000",
				Timestamp: "Mon Dec  5 20:47:31 2022\n",
				Msg:       "Check leader detects dangling pool.\n",
				ActChoices: []chkpb.CheckInconsistAction{
					chkpb.CheckInconsistAction_CIA_DISCARD,
					chkpb.CheckInconsistAction_CIA_IGNORE,
				},
				ActDetails: []string{
					"Discard the dangling pool entry from MS [suggested].",
					"Keep the dangling pool entry on MS, repair nothing.",
				},
				ActMsgs: []string{
					"Discard the unrecognized element: pool service, pool itself, container, and so on.",
					"Ignore but log the inconsistency.",
				},
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       973024752426024961,
					Class:     chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE,
					Action:    chkpb.CheckInconsistAction_CIA_INTERACT,
					PoolUuid:  "e0f1371c-087f-0000-ffff-ffff00000000",
					Timestamp: "Mon Dec  5 20:47:31 2022",
					Msg:       "Check leader detects dangling pool.",
					ActChoices: []chkpb.CheckInconsistAction{
						chkpb.CheckInconsistAction_CIA_DISCARD,
						chkpb.CheckInconsistAction_CIA_IGNORE,
					},
					ActDetails: []string{
						"Discard the dangling pool entry from MS [suggested].",
						"Keep the dangling pool entry on MS, repair nothing.",
					},
					ActMsgs: []string{
						"Discard the unrecognized element: pool service, pool itself, container, and so on.",
						"Ignore but log the inconsistency.",
					},
				}),
		},
		"interactive finding: pool label mismatch": {
			rpt: &chkpb.CheckReport{
				Seq:       973024752426024962,
				Class:     chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
				Action:    chkpb.CheckInconsistAction_CIA_INTERACT,
				PoolUuid:  "dfbb7b07-061f-46b2-a11c-935af3fb7169",
				Timestamp: "Mon Dec  5 20:47:31 2022\n",
				Msg:       "Check leader detects corrupted pool label: one-fault (MS) vs one (PS).\n",
				ActChoices: []chkpb.CheckInconsistAction{
					chkpb.CheckInconsistAction_CIA_TRUST_MS,
					chkpb.CheckInconsistAction_CIA_TRUST_PS,
					chkpb.CheckInconsistAction_CIA_IGNORE,
				},
				ActDetails: []string{
					"Inconsistent pool label: one-fault (MS) vs one (PS), Trust MS pool label [suggested]",
					"Trust PS pool label.",
					"Keep the inconsistent pool label, repair nothing.",
				},
				ActMsgs: []string{
					"Trust the information recorded in MS DB.",
					"Trust the information recorded in PS DB.",
					"Ignore but log the inconsistency.",
				},
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       973024752426024962,
					Class:     chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
					Action:    chkpb.CheckInconsistAction_CIA_INTERACT,
					PoolUuid:  "dfbb7b07-061f-46b2-a11c-935af3fb7169",
					Timestamp: "Mon Dec  5 20:47:31 2022",
					Msg:       "Check leader detects corrupted pool label: one-fault (MS) vs one (PS).",
					ActChoices: []chkpb.CheckInconsistAction{
						chkpb.CheckInconsistAction_CIA_TRUST_MS,
						chkpb.CheckInconsistAction_CIA_TRUST_PS,
						chkpb.CheckInconsistAction_CIA_IGNORE,
					},
					ActDetails: []string{
						"Inconsistent pool label: one-fault (MS) vs one (PS), Trust MS pool label [suggested]",
						"Trust PS pool label.",
						"Keep the inconsistent pool label, repair nothing.",
					},
					ActMsgs: []string{
						"Trust the information recorded in MS DB.",
						"Trust the information recorded in PS DB.",
						"Ignore but log the inconsistency.",
					},
				}),
		},
		"interactive finding: orphaned container storage": {
			rpt: &chkpb.CheckReport{
				Seq:       973024752426024964,
				Class:     chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
				Action:    chkpb.CheckInconsistAction_CIA_INTERACT,
				Rank:      1,
				PoolUuid:  "06a6ecff-5252-4ce3-a7e4-c7d875fb7495",
				ContUuid:  "fda598ea-6794-49ea-97af-ac787c2585a9",
				Timestamp: "Mon Dec  5 20:47:42 2022",
				Msg:       "Check engine detects orphan container 06a6ecff-5252-4ce3-a7e4-c7d875fb7495/fda598ea-6794-49ea-97af-ac787c2585a9",
				ActChoices: []chkpb.CheckInconsistAction{
					chkpb.CheckInconsistAction_CIA_DISCARD,
					chkpb.CheckInconsistAction_CIA_IGNORE,
				},
				ActDetails: []string{
					"Destroy the orphan container to release space [suggested].",
					"Keep the orphan container on engines, repair nothing.",
				},
				ActMsgs: []string{
					"Discard the unrecognized element: pool service, pool itself, container, and so on.",
					"Ignore but log the inconsistency.",
				},
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       973024752426024964,
					Class:     chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
					Action:    chkpb.CheckInconsistAction_CIA_INTERACT,
					Rank:      1,
					PoolUuid:  "06a6ecff-5252-4ce3-a7e4-c7d875fb7495",
					ContUuid:  "fda598ea-6794-49ea-97af-ac787c2585a9",
					Timestamp: "Mon Dec  5 20:47:42 2022",
					Msg:       "Check engine detects orphan container 06a6ecff-5252-4ce3-a7e4-c7d875fb7495/fda598ea-6794-49ea-97af-ac787c2585a9",
					ActChoices: []chkpb.CheckInconsistAction{
						chkpb.CheckInconsistAction_CIA_DISCARD,
						chkpb.CheckInconsistAction_CIA_IGNORE,
					},
					ActDetails: []string{
						"Destroy the orphan container to release space [suggested].",
						"Keep the orphan container on engines, repair nothing.",
					},
					ActMsgs: []string{
						"Discard the unrecognized element: pool service, pool itself, container, and so on.",
						"Ignore but log the inconsistency.",
					},
				}),
		},
		"interactive finding: container label mismatch": {
			rpt: &chkpb.CheckReport{
				Seq:       972775323717861377,
				Class:     chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
				Action:    chkpb.CheckInconsistAction_CIA_INTERACT,
				PoolUuid:  "9614ebfb-cbad-4250-a4e4-d24b7b70d85e",
				ContUuid:  "18b9b418-211c-455f-aa42-0cc13dedcff9",
				Timestamp: "Mon Dec  5 16:27:56 2022\n",
				Msg:       "Check engine detects inconsistent container label: new-label (CS) vs foo (property).\n",
				ActChoices: []chkpb.CheckInconsistAction{
					chkpb.CheckInconsistAction_CIA_TRUST_PS,
					chkpb.CheckInconsistAction_CIA_IGNORE,
					chkpb.CheckInconsistAction_CIA_TRUST_TARGET,
				},
				ActDetails: []string{
					"Repair the container label in container property [suggested].6",
					"Keep the inconsistent container label, repair nothing.0",
					"Repair the container label in container service.",
				},
				ActMsgs: []string{
					"Trust the information recorded in PS DB.",
					"Ignore but log the inconsistency.",
					"Trust the information recorded by target(s).",
				},
			},
			expFinding: checker.NewFinding(
				&chkpb.CheckReport{
					Seq:       972775323717861377,
					Class:     chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
					Action:    chkpb.CheckInconsistAction_CIA_INTERACT,
					PoolUuid:  "9614ebfb-cbad-4250-a4e4-d24b7b70d85e",
					ContUuid:  "18b9b418-211c-455f-aa42-0cc13dedcff9",
					Timestamp: "Mon Dec  5 16:27:56 2022",
					Msg:       "Check engine detects inconsistent container label: new-label (CS) vs foo (property).",
					ActChoices: []chkpb.CheckInconsistAction{
						chkpb.CheckInconsistAction_CIA_TRUST_PS,
						chkpb.CheckInconsistAction_CIA_IGNORE,
						chkpb.CheckInconsistAction_CIA_TRUST_TARGET,
					},
					ActDetails: []string{
						"Repair the container label in container property [suggested].6",
						"Keep the inconsistent container label, repair nothing.0",
						"Repair the container label in container service.",
					},
					ActMsgs: []string{
						"Trust the information recorded in PS DB.",
						"Ignore but log the inconsistency.",
						"Trust the information recorded by target(s).",
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
