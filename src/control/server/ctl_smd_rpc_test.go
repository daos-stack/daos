//
// (C) Copyright 2020-2022 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_CtlSvc_SmdQuery(t *testing.T) {
	stateUnplugged := storage.NvmeDevState(0).String()
	stateNew := storage.MockNvmeStateNew.String()
	stateNormal := storage.MockNvmeStateNormal.String()
	stateFaulty := storage.MockNvmeStateEvicted.String()
	stateIdentify := storage.MockNvmeStateIdentify.String()

	pbNormDev := &ctlpb.SmdDevResp_Device{
		Uuid:     common.MockUUID(),
		DevState: stateNormal,
	}
	pbFaultyQueryDev := &ctlpb.SmdQueryResp_Device{
		Uuid:     common.MockUUID(),
		DevState: stateFaulty,
	}
	pbIdentifyQueryDev := &ctlpb.SmdQueryResp_Device{
		Uuid:     common.MockUUID(),
		DevState: stateIdentify,
	}

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
							Devices: []*ctlpb.SmdDevResp_Device{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevStateResp{
							DevUuid:  common.MockUUID(),
							DevState: stateFaulty,
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Devices: []*ctlpb.SmdQueryResp_Device{pbFaultyQueryDev},
					},
				},
			},
		},
		"set-faulty; DAOS Failure": {
			req: &ctlpb.SmdQueryReq{
				SetFaulty: true,
				Uuid:      common.MockUUID(),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{pbNormDev},
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
		"identify": {
			req: &ctlpb.SmdQueryReq{
				Identify: true,
				Uuid:     common.MockUUID(),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevStateResp{
							DevUuid:  common.MockUUID(),
							DevState: stateIdentify,
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Devices: []*ctlpb.SmdQueryResp_Device{pbIdentifyQueryDev},
					},
				},
			},
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
		"list-pools; filter by rank": {
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
		"list-pools; filter by uuid": {
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
		"list-pools; DAOS Failure": {
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
									Uuid:     common.MockUUID(0),
									TrAddr:   "0000:8a:00.0",
									TgtIds:   []int32{0, 1, 2},
									DevState: stateNormal,
								},
								{
									Uuid:     common.MockUUID(1),
									TrAddr:   "0000:80:00.0",
									TgtIds:   []int32{3, 4, 5},
									DevState: stateFaulty,
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
									Uuid:     common.MockUUID(2),
									TrAddr:   "0000:da:00.0",
									TgtIds:   []int32{0, 1, 2},
									DevState: stateUnplugged,
								},
								{
									Uuid:     common.MockUUID(3),
									TrAddr:   "0000:db:00.0",
									TgtIds:   []int32{3, 4, 5},
									DevState: stateIdentify,
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
								Uuid:     common.MockUUID(0),
								TrAddr:   "0000:8a:00.0",
								TgtIds:   []int32{0, 1, 2},
								DevState: stateNormal,
							},
							{
								Uuid:     common.MockUUID(1),
								TrAddr:   "0000:80:00.0",
								TgtIds:   []int32{3, 4, 5},
								DevState: stateFaulty,
							},
						},
						Rank: uint32(0),
					},
					{
						Devices: []*ctlpb.SmdQueryResp_Device{
							{
								Uuid:     common.MockUUID(2),
								TrAddr:   "0000:da:00.0",
								TgtIds:   []int32{0, 1, 2},
								DevState: stateUnplugged,
							},
							{
								Uuid:     common.MockUUID(3),
								TrAddr:   "0000:db:00.0",
								TgtIds:   []int32{3, 4, 5},
								DevState: stateIdentify,
							},
						},
						Rank: uint32(1),
					},
				},
			},
		},
		"list-devices; missing state": {
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
									Uuid:   common.MockUUID(0),
									TrAddr: "0000:8a:00.0",
									TgtIds: []int32{0, 1, 2},
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
								Uuid:   common.MockUUID(0),
								TrAddr: "0000:8a:00.0",
								TgtIds: []int32{0, 1, 2},
							},
						},
						Rank: uint32(0),
					},
				},
			},
		},
		"list-devices; show only faulty": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(system.NilRank),
				StateMask: storage.NvmeStateFaulty.Uint32(),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevResp_Device{
								{
									Uuid:     common.MockUUID(0),
									TrAddr:   "0000:8a:00.0",
									TgtIds:   []int32{0, 1, 2},
									DevState: stateNormal,
								},
								{
									Uuid:     common.MockUUID(1),
									TrAddr:   "0000:8b:00.0",
									TgtIds:   []int32{3, 4, 5},
									DevState: stateFaulty,
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
									Uuid:     common.MockUUID(2),
									TrAddr:   "0000:da:00.0",
									TgtIds:   []int32{0, 1, 2},
									DevState: stateUnplugged,
								},
								{
									Uuid:     common.MockUUID(3),
									TrAddr:   "0000:db:00.0",
									TgtIds:   []int32{3, 4, 5},
									DevState: stateIdentify,
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
								Uuid:     common.MockUUID(1),
								TrAddr:   "0000:8b:00.0",
								TgtIds:   []int32{3, 4, 5},
								DevState: stateFaulty,
							},
						},
						Rank: uint32(0),
					},
					{
						Rank: uint32(1),
					},
				},
			},
		},
		"list-devices; filter by rank": {
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
									Uuid:     common.MockUUID(0),
									DevState: stateFaulty,
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
									Uuid:     common.MockUUID(1),
									DevState: stateNormal,
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
								Uuid:     common.MockUUID(1),
								DevState: stateNormal,
							},
						},
					},
				},
			},
		},
		"list-devices; filter by uuid": {
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
									Uuid:     common.MockUUID(0),
									DevState: stateNormal,
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
									Uuid:     common.MockUUID(1),
									DevState: stateFaulty,
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
								Uuid:     common.MockUUID(1),
								DevState: stateFaulty,
							},
						},
					},
				},
			},
		},
		"list-devices; DAOS Failure": {
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
									Uuid:     common.MockUUID(1),
									DevState: stateFaulty,
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
								Uuid:     common.MockUUID(1),
								DevState: stateFaulty,
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
		"device-health (NEW SMD); skip health collection": {
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
									Uuid:     common.MockUUID(0),
									DevState: stateNew,
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
									Uuid:     common.MockUUID(1),
									DevState: stateNew,
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
								Uuid:     common.MockUUID(1),
								DevState: storage.MockNvmeStateNew.String(),
							},
						},
					},
				},
			},
		},
		"device-health; DAOS Failure": {
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
									Uuid:     common.MockUUID(0),
									DevState: stateFaulty,
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
									Uuid:     common.MockUUID(1),
									DevState: stateFaulty,
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
									Uuid:     common.MockUUID(0),
									DevState: stateFaulty,
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
									Uuid:     common.MockUUID(1),
									TgtIds:   []int32{0},
									DevState: stateFaulty,
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
								Uuid:     common.MockUUID(1),
								TgtIds:   []int32{0},
								DevState: stateFaulty,
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
		"target-health; bad target": {
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
									Uuid:     common.MockUUID(0),
									DevState: stateNormal,
								},
							},
						},
					},
				},
			},
			expErr: errors.New("invalid"),
		},
		"target-health; missing rank": {
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
		"i/o engine not started": {
			req:       &ctlpb.SmdQueryReq{},
			ioStopped: true,
			expErr:    FaultDataPlaneNotStarted,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			engineCount := len(tc.drpcResps)
			if engineCount == 0 {
				engineCount = 1
			}

			cfg := config.DefaultServer()
			for i := 0; i < engineCount; i++ {
				cfg.Engines = append(cfg.Engines, engine.MockConfig().WithTargetCount(1).WithRank(uint32(i)))
			}
			svc := mockControlService(t, log, cfg, nil, nil, nil)
			svc.harness.started.SetTrue()

			for i, e := range svc.harness.instances {
				srv := e.(*EngineInstance)
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
					srv.(*EngineInstance).ready.SetFalse()
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
