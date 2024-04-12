//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
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
Pool space info:
- Target(VOS) count:0
`, uuid.Nil.String()),
		},
		"normal response": {
			pi: &daos.PoolInfo{
				State:            daos.PoolServiceStateDegraded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				Leader:           42,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool space info:
- Target(VOS) count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVMe):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
Rebuild busy, 42 objs, 21 recs
`, poolUUID.String()),
		},
		"normal response; enabled ranks": {
			pi: &daos.PoolInfo{
				State:            daos.PoolServiceStateDegraded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				Leader:           42,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool space info:
- Enabled ranks: 0-2
- Target(VOS) count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVMe):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
Rebuild busy, 42 objs, 21 recs
`, poolUUID.String()),
		},
		"normal response; disabled ranks": {
			pi: &daos.PoolInfo{
				State:            daos.PoolServiceStateDegraded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				Leader:           42,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool space info:
- Disabled ranks: 0-1,3
- Target(VOS) count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVMe):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
Rebuild busy, 42 objs, 21 recs
`, poolUUID.String()),
		},
		"unknown/invalid rebuild state response": {
			pi: &daos.PoolInfo{
				State:            daos.PoolServiceStateDegraded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				Leader:           42,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool space info:
- Disabled ranks: 0-1,3
- Target(VOS) count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVMe):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
Rebuild unknown, 42 objs, 21 recs
`, poolUUID.String()),
		},
		"rebuild failed": {
			pi: &daos.PoolInfo{
				State:            daos.PoolServiceStateDegraded,
				UUID:             poolUUID,
				TotalTargets:     2,
				DisabledTargets:  1,
				ActiveTargets:    1,
				Leader:           42,
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
Pool %s, ntarget=2, disabled=1, leader=42, version=100, state=Degraded
Pool layout out of date (1 < 2) -- see `+backtickStr+` for details.
Pool space info:
- Target(VOS) count:1
- Storage tier 0 (SCM):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- Storage tier 1 (NVMe):
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
Rebuild failed, status=2
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
