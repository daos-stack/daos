//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package pretty

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestPretty_PrintPoolQueryResp(t *testing.T) {
	for name, tc := range map[string]struct {
		pqr         *control.PoolQueryResp
		expPrintStr string
	}{
		"empty response": {
			pqr: &control.PoolQueryResp{},
			expPrintStr: `
Pool , ntarget=0, disabled=0, leader=0, version=0
Pool space info:
- Target(VOS) count:0
`,
		},
		"normal response": {
			pqr: &control.PoolQueryResp{
				UUID: common.MockUUID(),
				PoolInfo: control.PoolInfo{
					TotalTargets:    2,
					DisabledTargets: 1,
					ActiveTargets:   1,
					Leader:          42,
					Version:         100,
					Scm: &control.StorageUsageStats{
						Total: 2,
						Free:  1,
					},
					Nvme: &control.StorageUsageStats{
						Total: 2,
						Free:  1,
					},
					Rebuild: &control.PoolRebuildStatus{
						State:   control.PoolRebuildStateBusy,
						Objects: 42,
						Records: 21,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100
Pool space info:
- Target(VOS) count:1
- SCM:
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- NVMe:
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
Rebuild busy, 42 objs, 21 recs
`, common.MockUUID()),
		},
		"rebuild failed": {
			pqr: &control.PoolQueryResp{
				UUID: common.MockUUID(),
				PoolInfo: control.PoolInfo{
					TotalTargets:    2,
					DisabledTargets: 1,
					ActiveTargets:   1,
					Leader:          42,
					Version:         100,
					Scm: &control.StorageUsageStats{
						Total: 2,
						Free:  1,
					},
					Nvme: &control.StorageUsageStats{
						Total: 2,
						Free:  1,
					},
					Rebuild: &control.PoolRebuildStatus{
						Status:  2,
						State:   control.PoolRebuildStateBusy,
						Objects: 42,
						Records: 21,
					},
				},
			},
			expPrintStr: fmt.Sprintf(`
Pool %s, ntarget=2, disabled=1, leader=42, version=100
Pool space info:
- Target(VOS) count:1
- SCM:
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
- NVMe:
  Total size: 2 B
  Free: 1 B, min:0 B, max:0 B, mean:0 B
Rebuild failed, rc=0, status=2
`, common.MockUUID()),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintPoolQueryResponse(tc.pqr, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
