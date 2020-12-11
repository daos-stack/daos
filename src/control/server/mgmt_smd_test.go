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

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_MgmtSvc_SmdQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP        bool
		req            *mgmtpb.SmdQueryReq
		junkResp       bool
		drpcResps      map[int][]*mockDrpcResponse
		harnessStopped bool
		ioStopped      bool
		expResp        *mgmtpb.SmdQueryResp
		expErr         error
	}{
		"dRPC send fails": {
			req: &mgmtpb.SmdQueryReq{},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					&mockDrpcResponse{
						Message: &mgmtpb.SmdQueryReq{},
						Error:   errors.New("send failure"),
					},
				},
			},
			expErr: errors.New("send failure"),
		},
		"dRPC resp fails": {
			req:      &mgmtpb.SmdQueryReq{},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"set-faulty": {
			req: &mgmtpb.SmdQueryReq{
				SetFaulty: true,
				Uuid:      common.MockUUID(),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(),
								},
							},
						},
					},
					{
						Message: &mgmtpb.DevStateResp{
							DevUuid:  common.MockUUID(),
							DevState: "FAULTY",
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{
						Devices: []*mgmtpb.SmdQueryResp_Device{
							{
								Uuid:  common.MockUUID(),
								State: "FAULTY",
							},
						},
					},
				},
			},
		},
		"set-faulty (DAOS Failure)": {
			req: &mgmtpb.SmdQueryReq{
				SetFaulty: true,
				Uuid:      common.MockUUID(),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(),
								},
							},
						},
					},
					{
						Message: &mgmtpb.DevStateResp{
							Status: int32(drpc.DaosInvalidInput),
						},
					},
				},
			},
			expErr: drpc.DaosInvalidInput,
		},
		"list-pools": {
			req: &mgmtpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(system.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdPoolResp{
							Pools: []*mgmtpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(),
								},
							},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{
						Pools: []*mgmtpb.SmdQueryResp_Pool{
							{
								Uuid: common.MockUUID(),
							},
						},
					},
				},
			},
		},
		"list-pools (filter by rank)": {
			req: &mgmtpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        1,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdPoolResp{
							Pools: []*mgmtpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &mgmtpb.SmdPoolResp{
							Pools: []*mgmtpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(1),
								},
							},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{
						Rank: 1,
						Pools: []*mgmtpb.SmdQueryResp_Pool{
							{
								Uuid: common.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-pools (filter by uuid)": {
			req: &mgmtpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(system.NilRank),
				Uuid:        common.MockUUID(1),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdPoolResp{
							Pools: []*mgmtpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &mgmtpb.SmdPoolResp{
							Pools: []*mgmtpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(1),
								},
							},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{},
					{
						Rank: 1,
						Pools: []*mgmtpb.SmdQueryResp_Pool{
							{
								Uuid: common.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-pools (DAOS Failure)": {
			req: &mgmtpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(system.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdPoolResp{
							Status: int32(drpc.DaosBusy),
						},
					},
				},
			},
			expErr: drpc.DaosBusy,
		},
		"list-devices": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(system.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(),
								},
							},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{
						Devices: []*mgmtpb.SmdQueryResp_Device{
							{
								Uuid: common.MockUUID(),
							},
						},
					},
				},
			},
		},
		"list-devices (filter by rank)": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools: true,
				Rank:      1,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(1),
								},
							},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{
						Rank: 1,
						Devices: []*mgmtpb.SmdQueryResp_Device{
							{
								Uuid: common.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-devices (filter by uuid)": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(system.NilRank),
				Uuid:      common.MockUUID(1),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(1),
								},
							},
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{},
					{
						Rank: 1,
						Devices: []*mgmtpb.SmdQueryResp_Device{
							{
								Uuid: common.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-devices (DAOS Failure)": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(system.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Status: int32(drpc.DaosBusy),
						},
					},
				},
			},
			expErr: drpc.DaosBusy,
		},
		"device-health": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(system.NilRank),
				Uuid:             common.MockUUID(1),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid:  common.MockUUID(1),
									State: "FAULTY",
								},
							},
						},
					},
					{
						Message: &mgmtpb.BioHealthResp{
							Temperature: 1000000,
							TempWarn:    true,
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{},
					{
						Rank: 1,
						Devices: []*mgmtpb.SmdQueryResp_Device{
							{
								Uuid:  common.MockUUID(1),
								State: "FAULTY",
								Health: &mgmtpb.BioHealthResp{
									Temperature: 1000000,
									TempWarn:    true,
								},
							},
						},
					},
				},
			},
		},
		"device-health (DAOS Failure)": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(system.NilRank),
				Uuid:             common.MockUUID(1),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid:  common.MockUUID(1),
									State: "FAULTY",
								},
							},
						},
					},
					{
						Message: &mgmtpb.BioHealthResp{
							Status: int32(drpc.DaosFreeMemError),
						},
					},
				},
			},
			expErr: drpc.DaosFreeMemError,
		},
		"target-health": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             1,
				Target:           "0",
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid:   common.MockUUID(1),
									TgtIds: []int32{0},
									State:  "FAULTY",
								},
							},
						},
					},
					{
						Message: &mgmtpb.BioHealthResp{
							Temperature: 1000000,
							TempWarn:    true,
						},
					},
				},
			},
			expResp: &mgmtpb.SmdQueryResp{
				Ranks: []*mgmtpb.SmdQueryResp_RankResp{
					{
						Rank: 1,
						Devices: []*mgmtpb.SmdQueryResp_Device{
							{
								Uuid:   common.MockUUID(1),
								TgtIds: []int32{0},
								State:  "FAULTY",
								Health: &mgmtpb.BioHealthResp{
									Temperature: 1000000,
									TempWarn:    true,
								},
							},
						},
					},
				},
			},
		},
		"target-health (bad target)": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             0,
				Target:           "eleventy",
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &mgmtpb.SmdDevResp{
							Devices: []*mgmtpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
			},
			expErr: errors.New("invalid"),
		},
		"target-health (missing rank)": {
			req: &mgmtpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(system.NilRank),
				Target:           "0",
				IncludeBioHealth: true,
			},
			expErr: errors.New("invalid"),
		},
		"ambiguous UUID": {
			req: &mgmtpb.SmdQueryReq{
				Rank: uint32(system.NilRank),
				Uuid: common.MockUUID(),
			},
			expErr: errors.New("ambiguous"),
		},
		"harness not started": {
			req:            &mgmtpb.SmdQueryReq{},
			harnessStopped: true,
			expErr:         FaultHarnessNotStarted,
		},
		"i/o servers not started": {
			req:       &mgmtpb.SmdQueryReq{},
			ioStopped: true,
			expErr:    FaultDataPlaneNotStarted,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ioserverCount := len(tc.drpcResps)
			if ioserverCount == 0 {
				ioserverCount = 1
			}
			svc := newTestMgmtSvcMulti(t, log, ioserverCount, tc.setupAP)
			for i, srv := range svc.harness.instances {
				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					for _, mock := range tc.drpcResps[i] {
						cfg.setSendMsgResponseList(t, mock)
					}
				}
				srv.setDrpcClient(newMockDrpcClient(cfg))
			}
			if tc.harnessStopped {
				svc.harness.started.SetFalse()
			}
			if tc.ioStopped {
				for _, srv := range svc.harness.instances {
					srv.ready.SetFalse()
				}
			}

			gotResp, gotErr := svc.SmdQuery(context.TODO(), tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, common.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
