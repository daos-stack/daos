//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"fmt"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

func TestPretty_PrintPoolQueryTargetResp(t *testing.T) {
	for name, tc := range map[string]struct {
		pqtr        *control.PoolQueryTargetResp
		expPrintStr string
	}{
		"empty response": {
			pqtr:        &control.PoolQueryTargetResp{},
			expPrintStr: "\n",
		},
		"der_nonexist response (e.g., invalid target_idx input)": {
			pqtr: &control.PoolQueryTargetResp{
				Status: -1005,
			},
			expPrintStr: "\n",
		},
		"valid: single target (unknown, down_out)": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: control.PoolTargetStateDownOut,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state down_out
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, down)": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: control.PoolTargetStateDown,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state down
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, up)": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: control.PoolTargetStateUp,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state up
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, up_in)": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, new)": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: control.PoolTargetStateNew,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state new
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, drain)": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: control.PoolTargetStateDrain,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state drain
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: multiple target (mixed statuses exclude 2 targets in progress)": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: control.PoolTargetStateDown,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateDownOut,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state down
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state down_out
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"invalid target state": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: 42,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateDownOut,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state invalid
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state down_out
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"invalid target type": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  42,
						State: control.PoolTargetStateDown,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateDownOut,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type invalid, state down
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state down_out
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"three tiers; third tier unknown StorageMediaType": {
			pqtr: &control.PoolQueryTargetResp{
				Status: 0,
				Infos: []*control.PoolQueryTargetInfo{
					{
						Type:  0,
						State: control.PoolTargetStateDown,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
							{
								Total: 800000000000,
								Free:  200000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
							{
								Total: 800000000000,
								Free:  200000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateDownOut,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
							{
								Total: 800000000000,
								Free:  200000000000,
							},
						},
					},
					{
						Type:  0,
						State: control.PoolTargetStateUpIn,
						Space: []*daos.StorageTargetUsage{
							{
								Total: 6000000000,
								Free:  5000000000,
							},
							{
								Total: 100000000000,
								Free:  90000000000,
							},
							{
								Total: 800000000000,
								Free:  200000000000,
							},
						},
					},
				},
			},
			expPrintStr: `
Target: type unknown, state down
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
- Storage tier 2 (unknown):
  Total size: 800 GB
  Free: 200 GB
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
- Storage tier 2 (unknown):
  Total size: 800 GB
  Free: 200 GB
Target: type unknown, state down_out
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
- Storage tier 2 (unknown):
  Total size: 800 GB
  Free: 200 GB
Target: type unknown, state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVMe):
  Total size: 100 GB
  Free: 90 GB
- Storage tier 2 (unknown):
  Total size: 800 GB
  Free: 200 GB
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintPoolQueryTargetResponse(tc.pqtr, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func mockRanks(ranks ...uint32) []uint32 {
	return ranks
}

func TestPretty_PrintPoolCreateResp(t *testing.T) {
	for name, tc := range map[string]struct {
		pcr         *control.PoolCreateResp
		expPrintStr string
		expErr      error
	}{
		"nil response": {
			expErr: errors.New("nil response"),
		},
		"empty response": {
			pcr:    &control.PoolCreateResp{},
			expErr: errors.New("0 storage tiers"),
		},
		"basic": {
			pcr: &control.PoolCreateResp{
				UUID:     test.MockUUID(),
				SvcReps:  mockRanks(0, 1, 2),
				TgtRanks: mockRanks(0, 1, 2, 3),
				TierBytes: []uint64{
					600 * humanize.MByte,
					10 * humanize.GByte,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool created with 5.66%%,94.34%% storage tier ratio
-------------------------------------------------
  UUID                 : %s
  Service Leader       : 0                                   
  Service Ranks        : [0-2]                               
  Storage Ranks        : [0-3]                               
  Total Size           : 42 GB                               
  Storage tier 0 (SCM) : 2.4 GB (600 MB / rank)              
  Storage tier 1 (NVMe): 40 GB (10 GB / rank)                

`, test.MockPoolUUID()),
		},
		"no nvme": {
			pcr: &control.PoolCreateResp{
				UUID:     test.MockUUID(),
				SvcReps:  mockRanks(0, 1, 2),
				TgtRanks: mockRanks(0, 1, 2, 3),
				TierBytes: []uint64{
					600 * humanize.MByte,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool created with 100.00%% storage tier ratio
--------------------------------------------
  UUID                 : %s
  Service Leader       : 0                                   
  Service Ranks        : [0-2]                               
  Storage Ranks        : [0-3]                               
  Total Size           : 2.4 GB                              
  Storage tier 0 (SCM) : 2.4 GB (600 MB / rank)              

`, test.MockPoolUUID()),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			gotErr := PrintPoolCreateResponse(tc.pcr, &bld)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintListPoolsResponse(t *testing.T) {
	exampleTierStats := []*daos.StorageUsageStats{
		{
			MediaType: daos.StorageMediaTypeScm,
			Total:     100 * humanize.GByte,
			Free:      20 * humanize.GByte,
			Min:       5 * humanize.GByte,
			Max:       6 * humanize.GByte,
		},
		{
			MediaType: daos.StorageMediaTypeNvme,
			Total:     6 * humanize.TByte,
			Free:      1 * humanize.TByte,
			Min:       20 * humanize.GByte,
			Max:       50 * humanize.GByte,
		},
	}

	for name, tc := range map[string]struct {
		resp        *control.ListPoolsResp
		verbose     bool
		noQuery     bool
		expErr      error
		expPrintStr string
	}{
		"empty response": {
			resp:        &control.ListPoolsResp{},
			expPrintStr: msgNoPools + "\n",
		},
		"one pool; no usage": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						UUID:            test.MockPoolUUID(1),
						ServiceReplicas: []ranklist.Rank{0, 1, 2},
						State:           daos.PoolServiceStateReady,
					},
				},
			},
			expPrintStr: `
Pool     Size State Used Imbalance Disabled 
----     ---- ----- ---- --------- -------- 
00000001 0 B  Ready 0%   0%        0/0      

`,
		},
		"one pool; no uuid": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						ServiceReplicas: []ranklist.Rank{0, 1, 2},
						TierStats:       exampleTierStats,
					},
				},
			},
			expErr: errors.New("no uuid"),
		},
		"two pools; diff num tiers": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						UUID:             test.MockPoolUUID(1),
						ServiceReplicas:  []ranklist.Rank{0, 1, 2},
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    16,
						DisabledTargets:  0,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
					},
					{
						UUID:             test.MockPoolUUID(2),
						Label:            "one",
						ServiceReplicas:  []ranklist.Rank{3, 4, 5},
						TierStats:        exampleTierStats[1:],
						TotalTargets:     64,
						ActiveTargets:    56,
						DisabledTargets:  8,
						PoolLayoutVer:    2,
						UpgradeLayoutVer: 2,
					},
				},
			},
			expErr: errors.New("has 1 storage tiers, want 2"),
		},
		"two pools; only one labeled": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						UUID:             test.MockPoolUUID(1),
						ServiceReplicas:  []ranklist.Rank{0, 1, 2},
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    16,
						DisabledTargets:  0,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
					},
					{
						Label:            "two",
						UUID:             test.MockPoolUUID(2),
						ServiceReplicas:  []ranklist.Rank{3, 4, 5},
						TierStats:        exampleTierStats,
						TotalTargets:     64,
						ActiveTargets:    56,
						DisabledTargets:  8,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
					},
				},
			},
			expPrintStr: `
Pool     Size   State Used Imbalance Disabled UpgradeNeeded? 
----     ----   ----- ---- --------- -------- -------------- 
00000001 6.0 TB Ready 83%  16%       0/16     1->2           
two      6.0 TB Ready 83%  56%       8/64     1->2           

`,
		},
		"two pools; one SCM only": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						Label:            "one",
						UUID:             test.MockPoolUUID(1),
						ServiceReplicas:  []ranklist.Rank{0, 1, 2},
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    16,
						DisabledTargets:  0,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
					},
					{
						Label:           "two",
						UUID:            test.MockPoolUUID(2),
						ServiceReplicas: []ranklist.Rank{3, 4, 5},
						TierStats: []*daos.StorageUsageStats{
							exampleTierStats[0],
							{MediaType: daos.StorageMediaTypeNvme},
						},
						TotalTargets:     64,
						ActiveTargets:    56,
						DisabledTargets:  8,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    2,
						UpgradeLayoutVer: 2,
					},
				},
			},
			expPrintStr: `
Pool Size   State Used Imbalance Disabled UpgradeNeeded? 
---- ----   ----- ---- --------- -------- -------------- 
one  6.0 TB Ready 83%  16%       0/16     1->2           
two  100 GB Ready 80%  56%       8/64     None           

`,
		},
		"two pools; one failed query": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						Label:            "one",
						UUID:             test.MockPoolUUID(1),
						ServiceReplicas:  []ranklist.Rank{0, 1, 2},
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    16,
						DisabledTargets:  0,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
					},
					{
						Label:           "two",
						UUID:            test.MockPoolUUID(2),
						ServiceReplicas: []ranklist.Rank{3, 4, 5},
					},
				},
				QueryErrors: map[uuid.UUID]*control.PoolQueryErr{
					test.MockPoolUUID(2): {
						Error: errors.New("stats unavailable"),
					},
				},
			},
			expPrintStr: `
Query on pool "two" unsuccessful, error: "stats unavailable"

Pool Size   State Used Imbalance Disabled UpgradeNeeded? 
---- ----   ----- ---- --------- -------- -------------- 
one  6.0 TB Ready 83%  16%       0/16     1->2           

`,
		},
		"three pools; one failed query; one query bad status": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						Label:            "one",
						UUID:             test.MockPoolUUID(1),
						ServiceReplicas:  []ranklist.Rank{0, 1, 2},
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    16,
						DisabledTargets:  0,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 1,
					},
					{
						UUID:            test.MockPoolUUID(2),
						ServiceReplicas: []ranklist.Rank{3, 4, 5},
					},
					{
						Label:           "three",
						UUID:            test.MockPoolUUID(3),
						ServiceReplicas: []ranklist.Rank{3, 4, 5},
					},
				},
				QueryErrors: map[uuid.UUID]*control.PoolQueryErr{
					test.MockPoolUUID(2): {
						Error: errors.New("stats unavailable"),
					},
					test.MockPoolUUID(3): {
						Status: daos.NotInit,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Query on pool "00000002" unsuccessful, error: "stats unavailable"
Query on pool "three" unsuccessful, status: %q

Pool Size   State Used Imbalance Disabled 
---- ----   ----- ---- --------- -------- 
one  6.0 TB Ready 83%%  16%%       0/16     

`, daos.NotInit),
		},
		"verbose, empty response": {
			resp:        &control.ListPoolsResp{},
			verbose:     true,
			expPrintStr: msgNoPools + "\n",
		},
		"verbose; zero svc replicas": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						UUID:             test.MockPoolUUID(1),
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    16,
						DisabledTargets:  0,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
						Rebuild: &daos.PoolRebuildStatus{
							State: daos.PoolRebuildStateIdle,
						},
					},
				},
			},
			verbose: true,
			expPrintStr: `
Label UUID                                 State SvcReps SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State 
----- ----                                 ----- ------- -------- -------- ------------- --------- --------- -------------- -------- -------------- ------------- 
-     00000001-0001-0001-0001-000000000001 Ready N/A     100 GB   80 GB    16%           6.0 TB    5.0 TB    8%             0/16     1->2           idle          

`,
		},
		"verbose; zero svc replicas with no query": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						UUID:             test.MockPoolUUID(1),
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    16,
						DisabledTargets:  0,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
					},
				},
			},
			verbose: true,
			noQuery: true,
			expPrintStr: `
Label UUID                                 State SvcReps SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? 
----- ----                                 ----- ------- -------- -------- ------------- --------- --------- -------------- -------- -------------- 
-     00000001-0001-0001-0001-000000000001 Ready N/A     100 GB   80 GB    16%           6.0 TB    5.0 TB    8%             0/16     1->2           

`,
		},
		"verbose; two pools; one destroying": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						Label:            "one",
						UUID:             test.MockPoolUUID(1),
						ServiceReplicas:  []ranklist.Rank{0, 1, 2},
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    16,
						DisabledTargets:  0,
						State:            daos.PoolServiceStateReady,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
						Rebuild: &daos.PoolRebuildStatus{
							State: daos.PoolRebuildStateIdle,
						},
					},
					{
						Label:            "two",
						UUID:             test.MockPoolUUID(2),
						ServiceReplicas:  []ranklist.Rank{3, 4, 5},
						TierStats:        exampleTierStats,
						TotalTargets:     64,
						ActiveTargets:    56,
						DisabledTargets:  8,
						State:            daos.PoolServiceStateDestroying,
						PoolLayoutVer:    2,
						UpgradeLayoutVer: 2,
						Rebuild: &daos.PoolRebuildStatus{
							State: daos.PoolRebuildStateDone,
						},
					},
				},
			},
			verbose: true,
			expPrintStr: `
Label UUID                                 State      SvcReps SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State 
----- ----                                 -----      ------- -------- -------- ------------- --------- --------- -------------- -------- -------------- ------------- 
one   00000001-0001-0001-0001-000000000001 Ready      [0-2]   100 GB   80 GB    16%           6.0 TB    5.0 TB    8%             0/16     1->2           idle          
two   00000002-0002-0002-0002-000000000002 Destroying [3-5]   100 GB   80 GB    56%           6.0 TB    5.0 TB    27%            8/64     None           done          

`,
		},
		"verbose; one pools; rebuild state busy": {
			resp: &control.ListPoolsResp{
				Pools: []*daos.PoolInfo{
					{
						Label:            "one",
						UUID:             test.MockPoolUUID(1),
						ServiceReplicas:  []ranklist.Rank{0, 1, 2},
						TierStats:        exampleTierStats,
						TotalTargets:     16,
						ActiveTargets:    8,
						DisabledTargets:  8,
						State:            daos.PoolServiceStateDegraded,
						PoolLayoutVer:    1,
						UpgradeLayoutVer: 2,
						Rebuild: &daos.PoolRebuildStatus{
							State: daos.PoolRebuildStateBusy,
						},
					},
				},
			},
			verbose: true,
			expPrintStr: `
Label UUID                                 State    SvcReps SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State 
----- ----                                 -----    ------- -------- -------- ------------- --------- --------- -------------- -------- -------------- ------------- 
one   00000001-0001-0001-0001-000000000001 Degraded [0-2]   100 GB   80 GB    8%            6.0 TB    5.0 TB    4%             8/16     1->2           busy          

`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder

			// pass the same io writer to standard and error stream
			// parameters to mimic combined output seen on terminal
			err := PrintListPoolsResponse(&bld, &bld, tc.resp, tc.verbose, tc.noQuery)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
