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
	"errors"
	"fmt"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
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
			expErr: errors.New("target ranks"),
		},
		"basic": {
			pcr: &control.PoolCreateResp{
				UUID:      common.MockUUID(),
				SvcReps:   mockRanks(0, 1, 2),
				TgtRanks:  mockRanks(0, 1, 2, 3),
				ScmBytes:  600 * humanize.MByte,
				NvmeBytes: 10 * humanize.GByte,
			},
			expPrintStr: fmt.Sprintf(`
Pool created with 6.00%%%% SCM/NVMe ratio
---------------------------------------
  UUID          : %s
  Service Ranks : [0-2]                               
  Storage Ranks : [0-3]                               
  Total Size    : 11 GB                               
  SCM           : 600 MB (150 MB / rank)              
  NVMe          : 10 GB (2.5 GB / rank)               

`, common.MockUUID()),
		},
		"no nvme": {
			pcr: &control.PoolCreateResp{
				UUID:     common.MockUUID(),
				SvcReps:  mockRanks(0, 1, 2),
				TgtRanks: mockRanks(0, 1, 2, 3),
				ScmBytes: 600 * humanize.MByte,
			},
			expPrintStr: fmt.Sprintf(`
Pool created with 100.00%%%% SCM/NVMe ratio
-----------------------------------------
  UUID          : %s
  Service Ranks : [0-2]                               
  Storage Ranks : [0-3]                               
  Total Size    : 600 MB                              
  SCM           : 600 MB (150 MB / rank)              
  NVMe          : 0 B (0 B / rank)                    

`, common.MockUUID()),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			gotErr := PrintPoolCreateResponse(tc.pcr, &bld)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
