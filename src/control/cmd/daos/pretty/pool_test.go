//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

func TestPretty_PrintPoolInfo(t *testing.T) {
	poolUUID := test.MockPoolUUID()
	backtickStr := "`" + "dmg pool upgrade" + "`"
	for name, tc := range map[string]struct {
		pi          *daos.PoolInfo
		expPrintStr string
	}{
		"empty response": {
			pi: &daos.PoolInfo{},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=0, disabled=0, leader=0, version=0, state=Creating
Pool health info:
- No rebuild status available.
`, uuid.Nil.String()),
		},
		"normal response": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.DefaultPoolQueryMask,
				State:            daos.PoolServiceStateTargetsExcluded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				ServiceLeader:    42,
				Version:          100,
				PoolLayoutVer:    1,
				UpgradeLayoutVer: 2,
				Rebuild: &daos.PoolRebuildStatus{
					State:   daos.PoolRebuildStateBusy,
					Objects: 42,
					Records: 21,
				},
				TierStats: []*daos.StorageUsageStats{
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=TargetsExcluded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Rebuild busy, 42 objs, 21 recs
Pool space info:
- Target count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVME):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
`, poolUUID.String()),
		},
		"normal response; enabled ranks": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.DefaultPoolQueryMask,
				State:            daos.PoolServiceStateTargetsExcluded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				ServiceLeader:    42,
				Version:          100,
				PoolLayoutVer:    1,
				UpgradeLayoutVer: 2,
				EnabledRanks:     ranklist.MustCreateRankSet("[0,1,2]"),
				Rebuild: &daos.PoolRebuildStatus{
					State:   daos.PoolRebuildStateBusy,
					Objects: 42,
					Records: 21,
				},
				TierStats: []*daos.StorageUsageStats{
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=TargetsExcluded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Enabled ranks: 0-2
- Rebuild busy, 42 objs, 21 recs
Pool space info:
- Target count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVME):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
`, poolUUID.String()),
		},
		"normal response; dead ranks": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.HealthOnlyPoolQueryMask,
				State:            daos.PoolServiceStateTargetsExcluded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				ServiceLeader:    42,
				Version:          100,
				PoolLayoutVer:    1,
				UpgradeLayoutVer: 2,
				DisabledRanks:    ranklist.MustCreateRankSet("[0,1,3]"),
				DeadRanks:        ranklist.MustCreateRankSet("[2]"),
				Rebuild: &daos.PoolRebuildStatus{
					State:   daos.PoolRebuildStateBusy,
					Objects: 42,
					Records: 21,
				},
				TierStats: []*daos.StorageUsageStats{
					{
						Total: 2,
						Free:  1,
					},
					{
						Total: 2,
						Free:  1,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=TargetsExcluded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Disabled ranks: 0-1,3
- Dead ranks: 2
- Rebuild busy, 42 objs, 21 recs
`, poolUUID.String()),
		},
		"normal response; disabled ranks": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.DefaultPoolQueryMask,
				State:            daos.PoolServiceStateTargetsExcluded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				ServiceLeader:    42,
				Version:          100,
				PoolLayoutVer:    1,
				UpgradeLayoutVer: 2,
				DisabledRanks:    ranklist.MustCreateRankSet("[0,1,3]"),
				Rebuild: &daos.PoolRebuildStatus{
					State:   daos.PoolRebuildStateBusy,
					Objects: 42,
					Records: 21,
				},
				TierStats: []*daos.StorageUsageStats{
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=TargetsExcluded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Disabled ranks: 0-1,3
- Rebuild busy, 42 objs, 21 recs
Pool space info:
- Target count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVME):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
`, poolUUID.String()),
		},
		"unknown/invalid rebuild state response": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.DefaultPoolQueryMask,
				State:            daos.PoolServiceStateTargetsExcluded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				ServiceLeader:    42,
				Version:          100,
				PoolLayoutVer:    1,
				UpgradeLayoutVer: 2,
				DisabledRanks:    ranklist.MustCreateRankSet("[0,1,3]"),
				Rebuild: &daos.PoolRebuildStatus{
					State:   42,
					Objects: 42,
					Records: 21,
				},
				TierStats: []*daos.StorageUsageStats{
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=TargetsExcluded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Disabled ranks: 0-1,3
- Rebuild unknown, 42 objs, 21 recs
Pool space info:
- Target count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVME):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
`, poolUUID.String()),
		},
		"rebuild failing": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.DefaultPoolQueryMask,
				State:            daos.PoolServiceStateTargetsExcluded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				ServiceLeader:    42,
				Version:          100,
				PoolLayoutVer:    1,
				UpgradeLayoutVer: 2,
				Rebuild: &daos.PoolRebuildStatus{
					Status:       -2,
					State:        daos.PoolRebuildStateBusy,
					DerivedState: daos.PoolRebuildStateFailing,
					Objects:      42,
					Records:      21,
				},
				TierStats: []*daos.StorageUsageStats{
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
				MemFileBytes: humanize.GiByte,
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=TargetsExcluded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Rebuild failing (state=busy, status=-2)
Pool space info:
- Target count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVME):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
`, poolUUID.String()),
		},
		"normal response: MD-on-SSD": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.DefaultPoolQueryMask,
				State:            daos.PoolServiceStateTargetsExcluded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				ServiceLeader:    42,
				Version:          100,
				PoolLayoutVer:    1,
				UpgradeLayoutVer: 2,
				Rebuild: &daos.PoolRebuildStatus{
					State:   daos.PoolRebuildStateBusy,
					Objects: 42,
					Records: 21,
				},
				TierStats: []*daos.StorageUsageStats{
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     4,
						Free:      2,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
				MemFileBytes:  humanize.GiByte,
				MdOnSsdActive: true,
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=TargetsExcluded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Rebuild busy, 42 objs, 21 recs
Pool space info:
- Target count:1
- Total memory-file size: 1.1 GB
- Metadata storage:
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Data storage:
  Total size: 4 B
  Free: 2 B, min:0 B, max:0 B, mean:0 B
`, poolUUID.String()),
		},
		"rebuild state idle": {
			pi: &daos.PoolInfo{
				UUID:          poolUUID,
				TotalTargets:  8,
				ActiveTargets: 8,
				State:         daos.PoolServiceStateReady,
				Rebuild: &daos.PoolRebuildStatus{
					State:        daos.PoolRebuildStateIdle,
					DerivedState: daos.PoolRebuildStateIdle,
					Status:       0,
					Objects:      0,
					Records:      0,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=8, disabled=0, leader=0, version=0, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
`, poolUUID.String()),
		},
		"rebuild state stopped": {
			pi: &daos.PoolInfo{
				UUID:          poolUUID,
				TotalTargets:  8,
				ActiveTargets: 8,
				State:         daos.PoolServiceStateReady,
				Rebuild: &daos.PoolRebuildStatus{
					State:        daos.PoolRebuildStateDone,
					DerivedState: daos.PoolRebuildStateStopped,
					Status:       int32(daos.OpCanceled),
					Objects:      0,
					Records:      0,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=8, disabled=0, leader=0, version=0, state=Ready
Pool health info:
- Rebuild stopped (state=done, status=-2027)
`, poolUUID.String()),
		},
		"rebuild state done": {
			pi: &daos.PoolInfo{
				UUID:          poolUUID,
				TotalTargets:  8,
				ActiveTargets: 8,
				State:         daos.PoolServiceStateReady,
				Rebuild: &daos.PoolRebuildStatus{
					State:        daos.PoolRebuildStateDone,
					DerivedState: daos.PoolRebuildStateDone,
					Status:       0,
					Objects:      200,
					Records:      1000,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=8, disabled=0, leader=0, version=0, state=Ready
Pool health info:
- Rebuild done, 200 objs, 1000 recs
`, poolUUID.String()),
		},
		"rebuild state failed": {
			pi: &daos.PoolInfo{
				UUID:          poolUUID,
				TotalTargets:  8,
				ActiveTargets: 8,
				State:         daos.PoolServiceStateReady,
				Rebuild: &daos.PoolRebuildStatus{
					State:        daos.PoolRebuildStateDone,
					DerivedState: daos.PoolRebuildStateFailed,
					Status:       -1,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=8, disabled=0, leader=0, version=0, state=Ready
Pool health info:
- Rebuild failed (state=done, status=-1)
`, poolUUID.String()),
		},
		"rebuild state busy": {
			pi: &daos.PoolInfo{
				UUID:          poolUUID,
				TotalTargets:  8,
				ActiveTargets: 8,
				State:         daos.PoolServiceStateReady,
				Rebuild: &daos.PoolRebuildStatus{
					State:        daos.PoolRebuildStateBusy,
					DerivedState: daos.PoolRebuildStateBusy,
					Status:       0,
					Objects:      150,
					Records:      750,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=8, disabled=0, leader=0, version=0, state=Ready
Pool health info:
- Rebuild busy, 150 objs, 750 recs
`, poolUUID.String()),
		},
		"rebuild state stopping": {
			pi: &daos.PoolInfo{
				UUID:          poolUUID,
				TotalTargets:  8,
				ActiveTargets: 8,
				State:         daos.PoolServiceStateReady,
				Rebuild: &daos.PoolRebuildStatus{
					State:        daos.PoolRebuildStateBusy,
					DerivedState: daos.PoolRebuildStateStopping,
					Status:       int32(daos.OpCanceled),
					Objects:      100,
					Records:      500,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=8, disabled=0, leader=0, version=0, state=Ready
Pool health info:
- Rebuild stopping (state=busy, status=-2027)
`, poolUUID.String()),
		},
		"rebuild state failing": {
			pi: &daos.PoolInfo{
				UUID:          poolUUID,
				TotalTargets:  8,
				ActiveTargets: 8,
				State:         daos.PoolServiceStateReady,
				Rebuild: &daos.PoolRebuildStatus{
					State:        daos.PoolRebuildStateBusy,
					DerivedState: daos.PoolRebuildStateFailing,
					Status:       -1,
					Objects:      75,
					Records:      300,
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=8, disabled=0, leader=0, version=0, state=Ready
Pool health info:
- Rebuild failing (state=busy, status=-1)
`, poolUUID.String()),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintPoolInfo(tc.pi, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintPoolSelfHealDisable(t *testing.T) {
	for name, tc := range map[string]struct {
		poolSelfHeal string
		sysSelfHeal  string
		expPrintStr  string
	}{
		"defaults": {
			poolSelfHeal: "exclude;rebuild",
			sysSelfHeal:  "exclude;pool_exclude;pool_rebuild",
		},
		"no pool flags": {
			poolSelfHeal: "none",
			sysSelfHeal:  "exclude;pool_exclude;pool_rebuild",
			expPrintStr:  "exclude disabled on pool due to [pool] policy\nrebuild disabled on pool due to [pool] policy\n",
		},
		"no system flags": {
			poolSelfHeal: "exclude;rebuild",
			sysSelfHeal:  "none",
			expPrintStr:  "exclude disabled on pool due to [system] policy\nrebuild disabled on pool due to [system] policy\n",
		},
		"no flags": {
			poolSelfHeal: "none",
			sysSelfHeal:  "none",
			expPrintStr:  "exclude disabled on pool due to [pool system] policies\nrebuild disabled on pool due to [pool system] policies\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			PrintPoolSelfHealDisable(tc.poolSelfHeal, tc.sysSelfHeal, &bld)

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected print string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintPoolQueryTarget(t *testing.T) {
	for name, tc := range map[string]struct {
		pqti        *daos.PoolQueryTargetInfo
		expErr      error
		expPrintStr string
	}{
		"nil info": {
			expErr: errors.New("nil"),
		},
		"valid: single target (unknown, down_out)": {
			pqti: &daos.PoolQueryTargetInfo{
				State: daos.PoolTargetStateDownOut,
				Space: []*daos.StorageUsageStats{
					{
						Total:     6000000000,
						Free:      5000000000,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     100000000000,
						Free:      90000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: `
Target: state down_out
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVME):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, down)": {
			pqti: &daos.PoolQueryTargetInfo{
				State: daos.PoolTargetStateDown,
				Space: []*daos.StorageUsageStats{
					{
						Total:     6000000000,
						Free:      5000000000,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     100000000000,
						Free:      90000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: `
Target: state down
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVME):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, up)": {
			pqti: &daos.PoolQueryTargetInfo{
				State: daos.PoolTargetStateUp,
				Space: []*daos.StorageUsageStats{
					{
						Total:     6000000000,
						Free:      5000000000,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     100000000000,
						Free:      90000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: `
Target: state up
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVME):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, up_in)": {
			pqti: &daos.PoolQueryTargetInfo{
				State: daos.PoolTargetStateUpIn,
				Space: []*daos.StorageUsageStats{
					{
						Total:     6000000000,
						Free:      5000000000,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     100000000000,
						Free:      90000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: `
Target: state up_in
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVME):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, new)": {
			pqti: &daos.PoolQueryTargetInfo{
				State: daos.PoolTargetStateNew,
				Space: []*daos.StorageUsageStats{
					{
						Total:     6000000000,
						Free:      5000000000,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     100000000000,
						Free:      90000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: `
Target: state new
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVME):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, drain)": {
			pqti: &daos.PoolQueryTargetInfo{
				State: daos.PoolTargetStateDrain,
				Space: []*daos.StorageUsageStats{
					{
						Total:     6000000000,
						Free:      5000000000,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     100000000000,
						Free:      90000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
				MemFileBytes: 3000000000,
			},
			expPrintStr: `
Target: state drain
- Storage tier 0 (SCM):
  Total size: 6.0 GB
  Free: 5.0 GB
- Storage tier 1 (NVME):
  Total size: 100 GB
  Free: 90 GB
`,
		},
		"valid: single target (unknown, down_out): MD-on-SSD": {
			pqti: &daos.PoolQueryTargetInfo{
				State: daos.PoolTargetStateDownOut,
				Space: []*daos.StorageUsageStats{
					{
						Total:     6000000000,
						Free:      5000000000,
						MediaType: daos.StorageMediaTypeScm,
					},
					{
						Total:     100000000000,
						Free:      90000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
				MemFileBytes:  3000000000,
				MdOnSsdActive: true,
			},
			expPrintStr: `
Target: state down_out
- Metadata storage:
  Total size: 6.0 GB
  Free: 5.0 GB
- Data storage:
  Total size: 100 GB
  Free: 90 GB
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			gotErr := PrintPoolQueryTargetInfo(tc.pqti, &bld)
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

func TestPretty_PrintListPools(t *testing.T) {
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
		pools       []*daos.PoolInfo
		verbose     bool
		expErr      error
		expPrintStr string
	}{
		"empty list": {
			expPrintStr: `
No pools in system
`,
		},
		"one pool; no usage; default query-mask": {
			pools: []*daos.PoolInfo{
				{
					UUID:            test.MockPoolUUID(1),
					ServiceReplicas: []ranklist.Rank{0, 1, 2},
					State:           daos.PoolServiceStateReady,
					QueryMask:       daos.DefaultPoolQueryMask,
				},
			},
			expPrintStr: `
Pool     Size State Used Imbalance Disabled 
----     ---- ----- ---- --------- -------- 
00000001 0 B  Ready 0%   0%        0/0      

`,
		},
		"two pools; only one labeled; no query (zero query-mask)": {
			pools: []*daos.PoolInfo{
				{
					UUID:            test.MockPoolUUID(1),
					ServiceReplicas: []ranklist.Rank{0, 1, 2},
					State:           daos.PoolServiceStateReady,
				},
				{
					Label:           "two",
					UUID:            test.MockPoolUUID(2),
					ServiceReplicas: []ranklist.Rank{3, 4, 5},
					State:           daos.PoolServiceStateReady,
				},
			},
			expPrintStr: `
Pool     State 
----     ----- 
00000001 Ready 
two      Ready 

`,
		},
		"two pools; only one labeled; no query (zero query-mask); verbose": {
			verbose: true,
			pools: []*daos.PoolInfo{
				{
					UUID:            test.MockPoolUUID(1),
					ServiceReplicas: []ranklist.Rank{0, 1, 2},
					State:           daos.PoolServiceStateReady,
				},
				{
					Label:           "two",
					UUID:            test.MockPoolUUID(2),
					ServiceReplicas: []ranklist.Rank{3, 4, 5},
					State:           daos.PoolServiceStateReady,
				},
			},
			expPrintStr: `
Label UUID                                 State SvcReps 
----- ----                                 ----- ------- 
-     00000001-0001-0001-0001-000000000001 Ready [0-2]   
two   00000002-0002-0002-0002-000000000002 Ready [3-5]   

`,
		},
		"two pools; only one labeled; with query": {
			pools: []*daos.PoolInfo{
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
					QueryMask:        daos.DefaultPoolQueryMask,
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
					QueryMask:        daos.DefaultPoolQueryMask,
				},
			},
			expPrintStr: `
Pool     Size   State Used Imbalance Disabled UpgradeNeeded? 
----     ----   ----- ---- --------- -------- -------------- 
00000001 6.0 TB Ready 83%  8%        0/16     1->2           
two      6.0 TB Ready 83%  27%       8/64     1->2           

`,
		},
		"two pools; one SCM only": {
			pools: []*daos.PoolInfo{
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
					QueryMask:        daos.DefaultPoolQueryMask,
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
					QueryMask:        daos.DefaultPoolQueryMask,
				},
			},
			expPrintStr: `
Pool Size   State Used Imbalance Disabled UpgradeNeeded? 
---- ----   ----- ---- --------- -------- -------------- 
one  6.0 TB Ready 83%  8%        0/16     1->2           
two  100 GB Ready 80%  56%       8/64     None           

`,
		},
		"verbose, empty response": {
			verbose: true,
			expPrintStr: `
No pools in system
`,
		},
		"verbose; zero svc replicas": {
			pools: []*daos.PoolInfo{
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
					QueryMask: daos.DefaultPoolQueryMask,
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
			pools: []*daos.PoolInfo{
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
			verbose: true,
			expPrintStr: `
Label UUID                                 State SvcReps 
----- ----                                 ----- ------- 
-     00000001-0001-0001-0001-000000000001 Ready N/A     

`,
		},
		"verbose; two pools; one destroying": {
			pools: []*daos.PoolInfo{
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
					QueryMask: daos.DefaultPoolQueryMask,
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
					QueryMask: daos.DefaultPoolQueryMask,
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
		"verbose; one pool; rebuild state busy": {
			pools: []*daos.PoolInfo{
				{
					Label:            "one",
					UUID:             test.MockPoolUUID(1),
					ServiceReplicas:  []ranklist.Rank{0, 1, 2},
					TierStats:        exampleTierStats,
					TotalTargets:     16,
					ActiveTargets:    8,
					DisabledTargets:  8,
					State:            daos.PoolServiceStateTargetsExcluded,
					PoolLayoutVer:    1,
					UpgradeLayoutVer: 2,
					Rebuild: &daos.PoolRebuildStatus{
						State: daos.PoolRebuildStateBusy,
					},
					QueryMask:    daos.DefaultPoolQueryMask,
					MemFileBytes: 1,
				},
			},
			verbose: true,
			expPrintStr: `
Label UUID                                 State           SvcReps SCM Size SCM Used SCM Imbalance NVME Size NVME Used NVME Imbalance Disabled UpgradeNeeded? Rebuild State 
----- ----                                 -----           ------- -------- -------- ------------- --------- --------- -------------- -------- -------------- ------------- 
one   00000001-0001-0001-0001-000000000001 TargetsExcluded [0-2]   100 GB   80 GB    8%            6.0 TB    5.0 TB    4%             8/16     1->2           busy          

`,
		},
		"verbose; one pool; MD-on-SSD": {
			pools: []*daos.PoolInfo{
				{
					Label:            "one",
					UUID:             test.MockPoolUUID(1),
					ServiceReplicas:  []ranklist.Rank{0, 1, 2},
					TierStats:        exampleTierStats,
					TotalTargets:     16,
					ActiveTargets:    8,
					DisabledTargets:  8,
					State:            daos.PoolServiceStateTargetsExcluded,
					PoolLayoutVer:    1,
					UpgradeLayoutVer: 2,
					Rebuild: &daos.PoolRebuildStatus{
						State: daos.PoolRebuildStateDone,
					},
					QueryMask:     daos.DefaultPoolQueryMask,
					MemFileBytes:  1,
					MdOnSsdActive: true,
				},
			},
			verbose: true,
			expPrintStr: `
Label UUID                                 State           SvcReps Meta Size Meta Used Meta Imbalance Data Size Data Used Data Imbalance Disabled UpgradeNeeded? Rebuild State 
----- ----                                 -----           ------- --------- --------- -------------- --------- --------- -------------- -------- -------------- ------------- 
one   00000001-0001-0001-0001-000000000001 TargetsExcluded [0-2]   100 GB    80 GB     8%             6.0 TB    5.0 TB    4%             8/16     1->2           done          

`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder

			// pass the same io writer to standard and error stream
			// parameters to mimic combined output seen on terminal
			err := PrintPoolList(tc.pools, &bld, tc.verbose)
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
