//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty_test

import (
	"bytes"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestPretty_PrintCheckQueryResp(t *testing.T) {
	checkTime, err := time.Parse(time.RFC822Z, "20 Mar 23 10:07 -0500")
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		resp    *control.SystemCheckQueryResp
		verbose bool
		expOut  string
	}{
		"empty": {
			expOut: `
DAOS System Checker Info
  No results found.
`,
		},
		"(verbose) 2 pools being checked": {
			resp: &control.SystemCheckQueryResp{
				Status:    control.SystemCheckStatusRunning,
				ScanPhase: control.SystemCheckScanPhaseContainerList,
				StartTime: checkTime,
				Pools: map[string]*control.SystemCheckPoolInfo{
					"pool-1": {
						UUID:      "pool-1",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKING.String(),
						Phase:     chkpb.CheckScanPhase_CSP_PREPARE.String(),
						StartTime: checkTime,
					},
					"pool-2": {
						UUID:      "pool-2",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKING.String(),
						Phase:     chkpb.CheckScanPhase_CSP_PREPARE.String(),
						StartTime: checkTime,
					},
				},
			},
			verbose: true,
			expOut: `
DAOS System Checker Info
  Current status: RUNNING (started at: 2023-03-20T10:07:00.000-05:00)
  Current phase: CONT_LIST (Comparing container list on PS and storage nodes)
  Checking 2 pools

Per-Pool Checker Info:
  Pool pool-1: 0 ranks, status: CPS_CHECKING, phase: CSP_PREPARE, started: 2023-03-20T10:07:00.000-05:00
  Pool pool-2: 0 ranks, status: CPS_CHECKING, phase: CSP_PREPARE, started: 2023-03-20T10:07:00.000-05:00

No reports to display.
`,
		},
		"(verbose) 3 pools repaired; 1 unchecked; 1 removed": {
			resp: &control.SystemCheckQueryResp{
				Status:    control.SystemCheckStatusCompleted,
				ScanPhase: control.SystemCheckScanPhaseDone,
				Pools: map[string]*control.SystemCheckPoolInfo{
					"pool-1": {
						UUID:      "pool-1",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-2": {
						UUID:      "pool-2",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-3": {
						UUID:      "pool-3",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-5": {
						UUID:   "pool-5",
						Status: chkpb.CheckPoolStatus_CPS_UNCHECKED.String(),
						Phase:  chkpb.CheckScanPhase_CSP_PREPARE.String(),
					},
				},
				Reports: []*control.SystemCheckReport{
					{
						CheckReport: chkpb.CheckReport{
							Seq:      1,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_SVCL,
							Action:   chkpb.CheckInconsistAction_CIA_IGNORE,
							Msg:      "message 1",
							PoolUuid: "pool-1",
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      2,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
							Action:   chkpb.CheckInconsistAction_CIA_TRUST_MS,
							Msg:      "message 2",
							PoolUuid: "pool-2",
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      3,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_LESS_SVC_WITHOUT_QUORUM,
							Action:   chkpb.CheckInconsistAction_CIA_TRUST_PS,
							Msg:      "message 3",
							PoolUuid: "pool-3",
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      4,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE,
							Action:   chkpb.CheckInconsistAction_CIA_DISCARD,
							Msg:      "message 4",
							PoolUuid: "pool-4",
						},
					},
				},
			},
			verbose: true,
			expOut: `
DAOS System Checker Info
  Current status: COMPLETED
  Current phase: DONE (Check completed)
  Checked 4 pools

Per-Pool Checker Info:
  Pool pool-1: 0 ranks, status: CPS_CHECKED, phase: CSP_DONE, started: 2023-03-20T10:07:00.000-05:00
  Pool pool-2: 0 ranks, status: CPS_CHECKED, phase: CSP_DONE, started: 2023-03-20T10:07:00.000-05:00
  Pool pool-3: 0 ranks, status: CPS_CHECKED, phase: CSP_DONE, started: 2023-03-20T10:07:00.000-05:00
  Pool pool-5: 0 ranks, status: CPS_UNCHECKED, phase: CSP_PREPARE

Inconsistency Reports:
  ID:         0x1
  Class:      POOL_BAD_SVCL
  Message:    message 1
  Pool:       pool-1
  Resolution: IGNORE

  ID:         0x2
  Class:      POOL_BAD_LABEL
  Message:    message 2
  Pool:       pool-2
  Resolution: TRUST_MS

  ID:         0x3
  Class:      POOL_LESS_SVC_WITHOUT_QUORUM
  Message:    message 3
  Pool:       pool-3
  Resolution: TRUST_PS

  ID:         0x4
  Class:      POOL_NONEXIST_ON_ENGINE
  Message:    message 4
  Pool:       pool-4
  Resolution: DISCARD

`,
		},
		"non-verbose": {
			resp: &control.SystemCheckQueryResp{
				Status:    control.SystemCheckStatusCompleted,
				ScanPhase: control.SystemCheckScanPhaseDone,
				Pools: map[string]*control.SystemCheckPoolInfo{
					"pool-1": {
						UUID:      "pool-1",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-2": {
						UUID:      "pool-2",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-3": {
						UUID:      "pool-3",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-5": {
						UUID:   "pool-5",
						Status: chkpb.CheckPoolStatus_CPS_UNCHECKED.String(),
						Phase:  chkpb.CheckScanPhase_CSP_PREPARE.String(),
					},
				},
				Reports: []*control.SystemCheckReport{
					{
						CheckReport: chkpb.CheckReport{
							Seq:      1,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_SVCL,
							Action:   chkpb.CheckInconsistAction_CIA_IGNORE,
							Msg:      "message 1",
							PoolUuid: "pool-1",
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      2,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
							Action:   chkpb.CheckInconsistAction_CIA_TRUST_MS,
							Msg:      "message 2",
							PoolUuid: "pool-2",
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      3,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_LESS_SVC_WITHOUT_QUORUM,
							Action:   chkpb.CheckInconsistAction_CIA_TRUST_PS,
							Msg:      "message 3",
							PoolUuid: "pool-3",
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      4,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_NONEXIST_ON_ENGINE,
							Action:   chkpb.CheckInconsistAction_CIA_DISCARD,
							Msg:      "message 4",
							PoolUuid: "pool-4",
						},
					},
				},
			},
			expOut: `
DAOS System Checker Info
  Current status: COMPLETED
  Current phase: DONE (Check completed)
  Checked 4 pools

Inconsistency Reports:
- Resolved:
ID  Class                        Pool   Resolution 
--  -----                        ----   ---------- 
0x1 POOL_BAD_SVCL                pool-1 IGNORE     
0x2 POOL_BAD_LABEL               pool-2 TRUST_MS   
0x3 POOL_LESS_SVC_WITHOUT_QUORUM pool-3 TRUST_PS   
0x4 POOL_NONEXIST_ON_ENGINE      pool-4 DISCARD    

`,
		},
		"non-verbose with container": {
			resp: &control.SystemCheckQueryResp{
				Status:    control.SystemCheckStatusCompleted,
				ScanPhase: control.SystemCheckScanPhaseDone,
				Pools: map[string]*control.SystemCheckPoolInfo{
					"pool-1": {
						UUID:      "pool-1",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-2": {
						UUID:      "pool-2",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
				},
				Reports: []*control.SystemCheckReport{
					{
						CheckReport: chkpb.CheckReport{
							Seq:      1,
							Class:    chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
							Action:   chkpb.CheckInconsistAction_CIA_IGNORE,
							Msg:      "message 1",
							PoolUuid: "pool-1",
							ContUuid: "cont-1",
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      2,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
							Action:   chkpb.CheckInconsistAction_CIA_TRUST_MS,
							Msg:      "message 2",
							PoolUuid: "pool-2",
						},
					},
				},
			},
			expOut: `
DAOS System Checker Info
  Current status: COMPLETED
  Current phase: DONE (Check completed)
  Checked 2 pools

Inconsistency Reports:
- Resolved:
ID  Class               Pool   Cont   Resolution 
--  -----               ----   ----   ---------- 
0x1 CONT_NONEXIST_ON_PS pool-1 cont-1 IGNORE     
0x2 POOL_BAD_LABEL      pool-2 None   TRUST_MS   

`,
		},
		"non-verbose with resolved and interactive": {
			resp: &control.SystemCheckQueryResp{
				Status:    control.SystemCheckStatusCompleted,
				ScanPhase: control.SystemCheckScanPhaseDone,
				Pools: map[string]*control.SystemCheckPoolInfo{
					"pool-1": {
						UUID:      "pool-1",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-2": {
						UUID:      "pool-2",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
				},
				Reports: []*control.SystemCheckReport{
					{
						CheckReport: chkpb.CheckReport{
							Seq:      1,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
							Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
							Msg:      "message 1",
							PoolUuid: "pool-1",
							ActChoices: []chkpb.CheckInconsistAction{
								chkpb.CheckInconsistAction_CIA_TRUST_MS,
								chkpb.CheckInconsistAction_CIA_TRUST_PS,
							},
							ActDetails: []string{"trust MS details", "trust PS details"},
							ActMsgs:    []string{"trust MS", "trust PS"},
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      2,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
							Action:   chkpb.CheckInconsistAction_CIA_TRUST_MS,
							Msg:      "message 2",
							PoolUuid: "pool-2",
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      3,
							Class:    chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
							Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
							Msg:      "message 3",
							PoolUuid: "pool-2",
							ContUuid: "cont-1",
							ActChoices: []chkpb.CheckInconsistAction{
								chkpb.CheckInconsistAction_CIA_IGNORE,
								chkpb.CheckInconsistAction_CIA_TRUST_PS,
								chkpb.CheckInconsistAction_CIA_TRUST_MS,
							},
							ActDetails: []string{"ignore details", "trust PS details", "trust MS details"},
							ActMsgs:    []string{"ignore", "trust PS", "trust MS"},
						},
					},
				},
			},
			expOut: `
DAOS System Checker Info
  Current status: COMPLETED
  Current phase: DONE (Check completed)
  Checked 2 pools

Inconsistency Reports:
- Resolved:
ID  Class          Pool   Resolution 
--  -----          ----   ---------- 
0x2 POOL_BAD_LABEL pool-2 TRUST_MS   

- Action Required:
ID  Class               Pool   Cont   Repair Options      
--  -----               ----   ----   --------------      
0x1 POOL_BAD_LABEL      pool-1 None   0: trust MS details 
                                      1: trust PS details 
0x3 CONT_NONEXIST_ON_PS pool-2 cont-1 0: ignore details   
                                      1: trust PS details 
                                      2: trust MS details 

`,
		},
		"non-verbose with interactive only": {
			resp: &control.SystemCheckQueryResp{
				Status:    control.SystemCheckStatusCompleted,
				ScanPhase: control.SystemCheckScanPhaseDone,
				Pools: map[string]*control.SystemCheckPoolInfo{
					"pool-1": {
						UUID:      "pool-1",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
					"pool-2": {
						UUID:      "pool-2",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
				},
				Reports: []*control.SystemCheckReport{
					{
						CheckReport: chkpb.CheckReport{
							Seq:      1,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
							Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
							Msg:      "message 1",
							PoolUuid: "pool-1",
							ActChoices: []chkpb.CheckInconsistAction{
								chkpb.CheckInconsistAction_CIA_TRUST_MS,
								chkpb.CheckInconsistAction_CIA_TRUST_PS,
							},
							ActDetails: []string{"trust MS details", "trust PS details"},
							ActMsgs:    []string{"trust MS", "trust PS"},
						},
					},
					{
						CheckReport: chkpb.CheckReport{
							Seq:      3,
							Class:    chkpb.CheckInconsistClass_CIC_CONT_NONEXIST_ON_PS,
							Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
							Msg:      "message 3",
							PoolUuid: "pool-2",
							ContUuid: "cont-1",
							ActChoices: []chkpb.CheckInconsistAction{
								chkpb.CheckInconsistAction_CIA_IGNORE,
								chkpb.CheckInconsistAction_CIA_TRUST_PS,
								chkpb.CheckInconsistAction_CIA_TRUST_MS,
							},
							ActDetails: []string{"ignore details", "trust PS details", "trust MS details"},
							ActMsgs:    []string{"ignore", "trust PS", "trust MS"},
						},
					},
				},
			},
			expOut: `
DAOS System Checker Info
  Current status: COMPLETED
  Current phase: DONE (Check completed)
  Checked 2 pools

Inconsistency Reports:
- Action Required:
ID  Class               Pool   Cont   Repair Options      
--  -----               ----   ----   --------------      
0x1 POOL_BAD_LABEL      pool-1 None   0: trust MS details 
                                      1: trust PS details 
0x3 CONT_NONEXIST_ON_PS pool-2 cont-1 0: ignore details   
                                      1: trust PS details 
                                      2: trust MS details 

`,
		},
		"verbose interactive": {
			resp: &control.SystemCheckQueryResp{
				Status:    control.SystemCheckStatusCompleted,
				ScanPhase: control.SystemCheckScanPhaseDone,
				Pools: map[string]*control.SystemCheckPoolInfo{
					"pool-1": {
						UUID:      "pool-1",
						Status:    chkpb.CheckPoolStatus_CPS_CHECKED.String(),
						Phase:     chkpb.CheckScanPhase_CSP_DONE.String(),
						StartTime: checkTime,
					},
				},
				Reports: []*control.SystemCheckReport{
					{
						CheckReport: chkpb.CheckReport{
							Seq:      1,
							Class:    chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
							Action:   chkpb.CheckInconsistAction_CIA_INTERACT,
							Msg:      "message 1",
							PoolUuid: "pool-1",
							ActChoices: []chkpb.CheckInconsistAction{
								chkpb.CheckInconsistAction_CIA_TRUST_MS,
								chkpb.CheckInconsistAction_CIA_TRUST_PS,
							},
							ActDetails: []string{"trust MS details", "trust PS details"},
							ActMsgs:    []string{"trust MS", "trust PS"},
						},
					},
				},
			},
			verbose: true,
			expOut: `
DAOS System Checker Info
  Current status: COMPLETED
  Current phase: DONE (Check completed)
  Checked 1 pool

Per-Pool Checker Info:
  Pool pool-1: 0 ranks, status: CPS_CHECKED, phase: CSP_DONE, started: 2023-03-20T10:07:00.000-05:00

Inconsistency Reports:
  ID:         0x1
  Class:      POOL_BAD_LABEL
  Message:    message 1
  Pool:       pool-1
  Potential resolution actions:
    0: trust MS details
    1: trust PS details

`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var buf bytes.Buffer
			pretty.PrintCheckQueryResp(&buf, tc.resp, tc.verbose)
			got := buf.String()
			if diff := cmp.Diff(strings.TrimLeft(tc.expOut, "\n"), got); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s", diff)
			}
		})
	}
}
