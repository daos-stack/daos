//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"encoding/json"
	"fmt"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/common"
	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

func Test_Control_SystemCheckPoolInfo_JSON(t *testing.T) {
	startTime := time.Now()
	stopTime := startTime.Add(10 * time.Second)
	testUUID := test.MockUUID()

	for name, tc := range map[string]struct {
		in      *SystemCheckPoolInfo
		expJSON string
	}{
		"remaining": {
			in: &SystemCheckPoolInfo{
				RawRankInfo: map[ranklist.Rank]*mgmtpb.CheckQueryPool{
					0: {},
					1: {},
				},
				UUID:   testUUID,
				Status: "test",
				Phase:  "test",
				Time: CheckTime{
					StartTime: startTime,
					Remaining: 1 * time.Second,
				},
			},
			expJSON: `
{
  "uuid": "` + testUUID + `",
  "status": "test",
  "phase": "test",
  "time": {
    "started": "` + common.FormatTime(startTime) + `",
    "remaining": 1
  },
  "rank_count": 2
}`,
		},
		"stopped": {
			in: &SystemCheckPoolInfo{
				RawRankInfo: map[ranklist.Rank]*mgmtpb.CheckQueryPool{
					0: {},
					1: {},
				},
				UUID:   testUUID,
				Status: "test",
				Phase:  "test",
				Time: CheckTime{
					StartTime: startTime,
					StopTime:  stopTime,
				},
			},
			expJSON: `
{
  "uuid": "` + testUUID + `",
  "status": "test",
  "phase": "test",
  "time": {
    "started": "` + common.FormatTime(startTime) + `",
    "stopped": "` + common.FormatTime(stopTime) + `"
  },
  "rank_count": 2
}`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			actJSON, err := json.MarshalIndent(tc.in, "", "  ")
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(strings.TrimLeft(tc.expJSON, "\n"), string(actJSON)); diff != "" {
				t.Fatalf("unexpected JSON (-want, +got):\n%s", diff)
			}
		})
	}
}

func Test_Control_SystemCheckPoolInfo_String(t *testing.T) {
	startTime := time.Now()
	stopTime := startTime.Add(10 * time.Second)
	testUUID := test.MockUUID()

	for name, tc := range map[string]struct {
		in     *SystemCheckPoolInfo
		expStr string
	}{
		"remaining": {
			in: &SystemCheckPoolInfo{
				RawRankInfo: map[ranklist.Rank]*mgmtpb.CheckQueryPool{
					0: {},
					1: {},
				},
				UUID:   testUUID,
				Status: "test",
				Phase:  "test",
				Time: CheckTime{
					StartTime: startTime,
					Remaining: 1 * time.Second,
				},
			},
			expStr: fmt.Sprintf("Pool %s: 2 ranks, status: test, phase: test, started: %s, remaining: 1s", testUUID, common.FormatTime(startTime)),
		},
		"elapsed": {
			in: &SystemCheckPoolInfo{
				RawRankInfo: map[ranklist.Rank]*mgmtpb.CheckQueryPool{
					0: {},
					1: {},
				},
				UUID:   testUUID,
				Status: "test",
				Phase:  "test",
				Time: CheckTime{
					StartTime: startTime,
					StopTime:  stopTime,
				},
			},
			expStr: fmt.Sprintf("Pool %s: 2 ranks, status: test, phase: test, started: %s, stopped: %s (%s)", testUUID, common.FormatTime(startTime), common.FormatTime(stopTime), stopTime.Sub(startTime)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tc.expStr, tc.in.String()); diff != "" {
				t.Fatalf("unexpected String() output (-want, +got):\n%s", diff)
			}
		})
	}
}

func Test_Control_getPoolCheckInfo(t *testing.T) {
	testUuid1 := test.MockUUID(1)
	testUuid2 := test.MockUUID(2)
	startTime := time.Now()
	stopTime := startTime.Add(10 * time.Second)

	pbPool0_0 := &mgmtpb.CheckQueryPool{
		Uuid:   testUuid1,
		Status: chkpb.CheckPoolStatus_CPS_CHECKING,
		Phase:  chkpb.CheckScanPhase_CSP_POOL_LIST,
		Time: &mgmtpb.CheckQueryTime{
			StartTime: uint64(startTime.Unix()),
			MiscTime:  uint64(stopTime.Sub(startTime).Seconds()),
		},
		Targets: []*mgmtpb.CheckQueryTarget{
			{
				Rank: 1,
			},
		},
	}
	pbPool0_1 := &mgmtpb.CheckQueryPool{
		Uuid:   testUuid1,
		Status: chkpb.CheckPoolStatus_CPS_CHECKING,
		Phase:  chkpb.CheckScanPhase_CSP_POOL_LIST,
		Time: &mgmtpb.CheckQueryTime{
			StartTime: uint64(startTime.Unix()),
			MiscTime:  uint64(stopTime.Sub(startTime).Seconds()),
		},
		Targets: []*mgmtpb.CheckQueryTarget{
			{
				Rank: 2,
			},
		},
	}
	pbPool1_0 := &mgmtpb.CheckQueryPool{
		Uuid:   testUuid2,
		Status: chkpb.CheckPoolStatus_CPS_CHECKED,
		Phase:  chkpb.CheckScanPhase_CSP_DONE,
		Time: &mgmtpb.CheckQueryTime{
			StartTime: uint64(startTime.Unix()),
			MiscTime:  uint64(stopTime.Unix()),
		},
		Targets: []*mgmtpb.CheckQueryTarget{
			{
				Rank: 3,
			},
		},
	}

	for name, tc := range map[string]struct {
		pbPools  []*mgmtpb.CheckQueryPool
		expPools map[string]*SystemCheckPoolInfo
	}{
		"empty": {
			expPools: map[string]*SystemCheckPoolInfo{},
		},
		"two pools": {
			pbPools: []*mgmtpb.CheckQueryPool{
				pbPool0_0, pbPool1_0, pbPool0_1,
			},
			expPools: func() map[string]*SystemCheckPoolInfo {
				return map[string]*SystemCheckPoolInfo{
					testUuid1: {
						UUID:   testUuid1,
						Status: pbPool0_0.Status.String(),
						Phase:  pbPool0_0.Phase.String(),
						Time: CheckTime{
							StartTime: startTime,
							Remaining: stopTime.Sub(startTime),
						},
						RawRankInfo: map[ranklist.Rank]*mgmtpb.CheckQueryPool{
							1: pbPool0_0,
							2: pbPool0_1,
						},
					},
					testUuid2: {
						UUID:   testUuid2,
						Status: pbPool1_0.Status.String(),
						Phase:  pbPool1_0.Phase.String(),
						Time: CheckTime{
							StartTime: startTime,
							StopTime:  stopTime,
						},
						RawRankInfo: map[ranklist.Rank]*mgmtpb.CheckQueryPool{
							3: pbPool1_0,
						},
					},
				}
			}(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			actPools := getPoolCheckInfo(tc.pbPools)
			cmpOpts := []cmp.Option{
				cmpopts.EquateApproxTime(1 * time.Second),
				protocmp.Transform(),
			}
			if diff := cmp.Diff(tc.expPools, actPools, cmpOpts...); diff != "" {
				t.Fatalf("unexpected pool info (-want, +got):\n%s", diff)
			}
		})
	}
}
