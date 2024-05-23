//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestPretty_PrintSetEngineLogMasksResp(t *testing.T) {
	for name, tc := range map[string]struct {
		resp      *control.SetEngineLogMasksResp
		expStdout string
		expStderr string
		expErr    error
	}{
		"empty response": {
			resp:      new(control.SetEngineLogMasksResp),
			expStdout: ``,
		},
		"server error": {
			resp: &control.SetEngineLogMasksResp{
				HostErrorsResp: control.MockHostErrorsResp(t,
					&control.MockHostError{
						Hosts: "host1",
						Error: "failed",
					}),
				HostStorage: nil,
			},
			expStderr: `
Errors:
  Hosts Error  
  ----- -----  
  host1 failed 

`,
		},
		"one pass; one fail": {
			resp: &control.SetEngineLogMasksResp{
				HostErrorsResp: control.MockHostErrorsResp(t,
					&control.MockHostError{
						Hosts: "host1",
						Error: "engine-0: drpc fails, engine-1: updated",
					}),
				HostStorage: control.MockHostStorageMap(t,
					&control.MockStorageScan{
						Hosts:    "host2",
						HostScan: control.MockServerScanResp(t, "standard"),
					}),
			},
			expStdout: `
Engine log-masks updated successfully on the following host: host2
`,
			expStderr: `
Errors:
  Hosts Error                                   
  ----- -----                                   
  host1 engine-0: drpc fails, engine-1: updated 

`,
		},
		"two passes": {
			resp: &control.SetEngineLogMasksResp{
				HostStorage: control.MockHostStorageMap(t,
					&control.MockStorageScan{
						Hosts:    "host[1-2]",
						HostScan: control.MockServerScanResp(t, "standard"),
					}),
			},
			expStdout: `
Engine log-masks updated successfully on the following hosts: host[1-2]
`,
		},
		"two failures": {
			resp: &control.SetEngineLogMasksResp{
				HostErrorsResp: control.MockHostErrorsResp(t,
					&control.MockHostError{
						Hosts: "host[1-2]",
						Error: "engine-0: drpc fails, engine-1: not ready",
					}),
			},
			expStderr: `
Errors:
  Hosts     Error                                     
  -----     -----                                     
  host[1-2] engine-0: drpc fails, engine-1: not ready 

`,
		},
		"multiple scan entries in map": {
			resp: &control.SetEngineLogMasksResp{
				HostStorage: control.MockHostStorageMap(t,
					&control.MockStorageScan{
						Hosts:    "host[1-2]",
						HostScan: control.MockServerScanResp(t, "standard"),
					},
					&control.MockStorageScan{
						Hosts:    "host[3-4]",
						HostScan: control.MockServerScanResp(t, "noStorage"),
					}),
			},
			expErr: errors.New("unexpected number of keys"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var out, outErr strings.Builder

			gotErr := PrintSetEngineLogMasksResp(tc.resp, &out, &outErr)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expStdout, "\n"), out.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(strings.TrimLeft(tc.expStderr, "\n"), outErr.String()); diff != "" {
				t.Fatalf("unexpected print output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
