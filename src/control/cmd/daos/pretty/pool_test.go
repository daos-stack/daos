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
				State:            daos.PoolServiceStateDegraded,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
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
				State:            daos.PoolServiceStateDegraded,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
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
		"normal response; disabled ranks": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.DefaultPoolQueryMask,
				State:            daos.PoolServiceStateDegraded,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
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
				State:            daos.PoolServiceStateDegraded,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
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
		"rebuild failed": {
			pi: &daos.PoolInfo{
				QueryMask:        daos.DefaultPoolQueryMask,
				State:            daos.PoolServiceStateDegraded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				ServiceLeader:    42,
				Version:          100,
				PoolLayoutVer:    1,
				UpgradeLayoutVer: 2,
				Rebuild: &daos.PoolRebuildStatus{
					Status:  2,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Rebuild failed, status=2
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
				State:            daos.PoolServiceStateDegraded,
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
						MediaType: daos.StorageMediaTypeNvme,
					},
					{
						Total:     2,
						Free:      1,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool health info:
- Rebuild busy, 42 objs, 21 recs
Pool space info:
- Target count:1
- Total memory-file size: 2 B
- Metadata storage:
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Data storage:
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
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
				Type:  0,
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
Target: type unknown, state down_out
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
				Type:  0,
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
Target: type unknown, state down
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
				Type:  0,
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
Target: type unknown, state up
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
				Type:  0,
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
Target: type unknown, state up_in
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
				Type:  0,
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
Target: type unknown, state new
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
				Type:  0,
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
			},
			expPrintStr: `
Target: type unknown, state drain
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
				Type:  0,
				State: daos.PoolTargetStateDownOut,
				Space: []*daos.StorageUsageStats{
					{
						Total:     6000000000,
						Free:      5000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
					{
						Total:     100000000000,
						Free:      90000000000,
						MediaType: daos.StorageMediaTypeNvme,
					},
				},
			},
			expPrintStr: `
Target: type unknown, state down_out
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
