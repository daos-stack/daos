//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_CtlSvc_SmdQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		setupAP        bool
		req            *ctlpb.SmdQueryReq
		junkResp       bool
		drpcResps      map[int][]*mockDrpcResponse
		harnessStopped bool
		ioStopped      bool
		expResp        *ctlpb.SmdQueryResp
		expErr         error
	}{
		"dRPC send fails": {
			req: &ctlpb.SmdQueryReq{},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					&mockDrpcResponse{
						Message: &ctlpb.SmdQueryReq{},
						Error:   errors.New("send failure"),
					},
				},
			},
			expErr: errors.New("send failure"),
		},
		"dRPC resp fails": {
			req:      &ctlpb.SmdQueryReq{},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"set-faulty": {
			req: &ctlpb.SmdQueryReq{
				SetFaulty: true,
				Uuid:      common.MockUUID(),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(),
								},
							},
						},
					},
					{
						Message: &ctlpb.DevStateResp{
							DevUuid:  common.MockUUID(),
							DevState: "FAULTY",
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Devices: []*ctlpb.SmdQueryResp_Device{
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
			req: &ctlpb.SmdQueryReq{
				SetFaulty: true,
				Uuid:      common.MockUUID(),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(),
								},
							},
						},
					},
					{
						Message: &ctlpb.DevStateResp{
							Status: int32(drpc.DaosInvalidInput),
						},
					},
				},
			},
			expErr: drpc.DaosInvalidInput,
		},
		"list-pools": {
			req: &ctlpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(system.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdPoolResp{
							Pools: []*ctlpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(),
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Pools: []*ctlpb.SmdQueryResp_Pool{
							{
								Uuid: common.MockUUID(),
							},
						},
					},
				},
			},
		},
		"list-pools (filter by rank)": {
			req: &ctlpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        1,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdPoolResp{
							Pools: []*ctlpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdPoolResp{
							Pools: []*ctlpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(1),
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Rank: 1,
						Pools: []*ctlpb.SmdQueryResp_Pool{
							{
								Uuid: common.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-pools (filter by uuid)": {
			req: &ctlpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(system.NilRank),
				Uuid:        common.MockUUID(1),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdPoolResp{
							Pools: []*ctlpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdPoolResp{
							Pools: []*ctlpb.SmdPoolResp_Pool{
								{
									Uuid: common.MockUUID(1),
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{},
					{
						Rank: 1,
						Pools: []*ctlpb.SmdQueryResp_Pool{
							{
								Uuid: common.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-pools (DAOS Failure)": {
			req: &ctlpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(system.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdPoolResp{
							Status: int32(drpc.DaosBusy),
						},
					},
				},
			},
			expErr: drpc.DaosBusy,
		},
		"list-devices": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(system.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(),
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Devices: []*ctlpb.SmdQueryResp_Device{
							{
								Uuid: common.MockUUID(),
							},
						},
					},
				},
			},
		},
		"list-devices (filter by rank)": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      1,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(1),
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Rank: 1,
						Devices: []*ctlpb.SmdQueryResp_Device{
							{
								Uuid: common.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-devices (filter by uuid)": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(system.NilRank),
				Uuid:      common.MockUUID(1),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(1),
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{},
					{
						Rank: 1,
						Devices: []*ctlpb.SmdQueryResp_Device{
							{
								Uuid: common.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-devices (DAOS Failure)": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(system.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Status: int32(drpc.DaosBusy),
						},
					},
				},
			},
			expErr: drpc.DaosBusy,
		},
		"device-health": {
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(system.NilRank),
				Uuid:             common.MockUUID(1),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid:  common.MockUUID(1),
									State: "FAULTY",
								},
							},
						},
					},
					{
						Message: &ctlpb.BioHealthResp{
							Temperature: 1000000,
							TempWarn:    true,
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{},
					{
						Rank: 1,
						Devices: []*ctlpb.SmdQueryResp_Device{
							{
								Uuid:  common.MockUUID(1),
								State: "FAULTY",
								Health: &ctlpb.BioHealthResp{
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
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(system.NilRank),
				Uuid:             common.MockUUID(1),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid:  common.MockUUID(1),
									State: "FAULTY",
								},
							},
						},
					},
					{
						Message: &ctlpb.BioHealthResp{
							Status: int32(drpc.DaosFreeMemError),
						},
					},
				},
			},
			expErr: drpc.DaosFreeMemError,
		},
		"target-health": {
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             1,
				Target:           "0",
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid: common.MockUUID(0),
								},
							},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid:   common.MockUUID(1),
									TgtIds: []int32{0},
									State:  "FAULTY",
								},
							},
						},
					},
					{
						Message: &ctlpb.BioHealthResp{
							Temperature: 1000000,
							TempWarn:    true,
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Rank: 1,
						Devices: []*ctlpb.SmdQueryResp_Device{
							{
								Uuid:   common.MockUUID(1),
								TgtIds: []int32{0},
								State:  "FAULTY",
								Health: &ctlpb.BioHealthResp{
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
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             0,
				Target:           "eleventy",
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
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
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(system.NilRank),
				Target:           "0",
				IncludeBioHealth: true,
			},
			expErr: errors.New("invalid"),
		},
		"ambiguous UUID": {
			req: &ctlpb.SmdQueryReq{
				Rank: uint32(system.NilRank),
				Uuid: common.MockUUID(),
			},
			expErr: errors.New("ambiguous"),
		},
		"harness not started": {
			req:            &ctlpb.SmdQueryReq{},
			harnessStopped: true,
			expErr:         FaultHarnessNotStarted,
		},
		"i/o servers not started": {
			req:       &ctlpb.SmdQueryReq{},
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

			cfg := config.DefaultServer()
			for i := 0; i < ioserverCount; i++ {
				cfg.Servers = append(cfg.Servers, ioserver.NewConfig().WithTargetCount(1).WithRank(uint32(i)))
			}
			svc := mockControlService(t, log, cfg, nil, nil, nil)
			svc.harness.started.SetTrue()

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
				srv.ready.SetTrue()
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
