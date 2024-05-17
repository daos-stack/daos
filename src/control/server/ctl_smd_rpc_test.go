//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
)

const (
	devStateNew    = ctlpb.NvmeDevState_NEW
	devStateNormal = ctlpb.NvmeDevState_NORMAL
	devStateFaulty = ctlpb.NvmeDevState_EVICTED
	devStateUnplug = ctlpb.NvmeDevState_UNPLUGGED

	ledStateIdentify = ctlpb.LedState_QUICK_BLINK
	ledStateNormal   = ctlpb.LedState_OFF
	ledStateFault    = ctlpb.LedState_ON
	ledStateUnknown  = ctlpb.LedState_NA
)

func pbNewDev(i int32) *ctlpb.SmdDevice {
	return &ctlpb.SmdDevice{
		Uuid: test.MockUUID(i),
		Ctrlr: &ctlpb.NvmeController{
			PciAddr:  test.MockPCIAddr(i),
			DevState: devStateNew,
			LedState: ledStateNormal,
		},
	}
}
func pbNormDev(i int32) *ctlpb.SmdDevice {
	return &ctlpb.SmdDevice{
		Uuid: test.MockUUID(i),
		Ctrlr: &ctlpb.NvmeController{
			PciAddr:  test.MockPCIAddr(i),
			DevState: devStateNormal,
			LedState: ledStateNormal,
		},
	}
}
func pbFaultDev(i int32) *ctlpb.SmdDevice {
	return &ctlpb.SmdDevice{
		Uuid: test.MockUUID(i),
		Ctrlr: &ctlpb.NvmeController{
			PciAddr:  test.MockPCIAddr(i),
			DevState: devStateFaulty,
			LedState: ledStateFault,
		},
	}
}
func pbIdentDev(i int32) *ctlpb.SmdDevice {
	return &ctlpb.SmdDevice{
		Uuid: test.MockUUID(i),
		Ctrlr: &ctlpb.NvmeController{
			PciAddr:  test.MockPCIAddr(i),
			DevState: devStateNormal,
			LedState: ledStateIdentify,
		},
	}
}
func pbDevWithHealth(sd *ctlpb.SmdDevice, h *ctlpb.BioHealthResp) *ctlpb.SmdDevice {
	sd.Ctrlr.HealthStats = h
	return sd
}

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
		"list-pools": {
			req: &ctlpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(ranklist.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdPoolResp{
							Pools: []*ctlpb.SmdPoolResp_Pool{
								{
									Uuid: test.MockUUID(),
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
								Uuid: test.MockUUID(),
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
									Uuid: test.MockUUID(0),
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
									Uuid: test.MockUUID(1),
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
								Uuid: test.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-pools; filter by uuid": {
			req: &ctlpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(ranklist.NilRank),
				Uuid:        test.MockUUID(1),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdPoolResp{
							Pools: []*ctlpb.SmdPoolResp_Pool{
								{
									Uuid: test.MockUUID(0),
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
									Uuid: test.MockUUID(1),
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
								Uuid: test.MockUUID(1),
							},
						},
					},
				},
			},
		},
		"list-pools; DAOS Failure": {
			req: &ctlpb.SmdQueryReq{
				OmitDevices: true,
				Rank:        uint32(ranklist.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdPoolResp{
							Status: int32(daos.Busy),
						},
					},
				},
			},
			expErr: daos.Busy,
		},
		"list-devices": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(ranklist.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:   test.MockUUID(0),
									TgtIds: []int32{0, 1, 2},
									Ctrlr: &ctlpb.NvmeController{
										PciAddr:  "0000:8a:00.0",
										DevState: devStateNormal,
										LedState: ledStateNormal,
									},
								},
								{
									Uuid:   test.MockUUID(1),
									TgtIds: []int32{3, 4, 5},
									Ctrlr: &ctlpb.NvmeController{
										PciAddr:  "0000:80:00.0",
										DevState: devStateFaulty,
										LedState: ledStateFault,
									},
								},
								{
									Uuid:   test.MockUUID(2),
									TgtIds: []int32{},
									Ctrlr: &ctlpb.NvmeController{
										PciAddr:  "0000:8b:00.0",
										DevState: devStateUnplug,
										LedState: ledStateUnknown,
									},
								},
							},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:   test.MockUUID(3),
									TgtIds: []int32{0, 1, 2},
									Ctrlr: &ctlpb.NvmeController{
										PciAddr:  "0000:da:00.0",
										DevState: devStateFaulty,
										LedState: ledStateUnknown,
									},
								},
								{
									Uuid:   test.MockUUID(4),
									TgtIds: []int32{3, 4, 5},
									Ctrlr: &ctlpb.NvmeController{
										PciAddr:  "0000:db:00.0",
										DevState: devStateNormal,
										LedState: ledStateIdentify,
									},
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Devices: []*ctlpb.SmdDevice{
							{
								Uuid:   test.MockUUID(0),
								TgtIds: []int32{0, 1, 2},
								Ctrlr: &ctlpb.NvmeController{
									PciAddr:  "0000:8a:00.0",
									DevState: devStateNormal,
									LedState: ledStateNormal,
								},
							},
							{
								Uuid:   test.MockUUID(1),
								TgtIds: []int32{3, 4, 5},
								Ctrlr: &ctlpb.NvmeController{
									PciAddr:  "0000:80:00.0",
									DevState: devStateFaulty,
									LedState: ledStateFault,
								},
							},
							{
								Uuid:   test.MockUUID(2),
								TgtIds: []int32{},
								Ctrlr: &ctlpb.NvmeController{
									PciAddr:  "0000:8b:00.0",
									DevState: devStateUnplug,
									LedState: ledStateUnknown,
								},
							},
						},
						Rank: uint32(0),
					},
					{
						Devices: []*ctlpb.SmdDevice{
							{
								Uuid:   test.MockUUID(3),
								TgtIds: []int32{0, 1, 2},
								Ctrlr: &ctlpb.NvmeController{
									PciAddr:  "0000:da:00.0",
									DevState: devStateFaulty,
									LedState: ledStateUnknown,
								},
							},
							{
								Uuid:   test.MockUUID(4),
								TgtIds: []int32{3, 4, 5},
								Ctrlr: &ctlpb.NvmeController{
									PciAddr:  "0000:db:00.0",
									DevState: devStateNormal,
									LedState: ledStateIdentify,
								},
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
				Rank:      uint32(ranklist.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:   test.MockUUID(0),
									TgtIds: []int32{0, 1, 2},
									Ctrlr: &ctlpb.NvmeController{
										PciAddr: "0000:db:00.0",
									},
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Devices: []*ctlpb.SmdDevice{
							{
								Uuid:   test.MockUUID(0),
								TgtIds: []int32{0, 1, 2},
								Ctrlr: &ctlpb.NvmeController{
									PciAddr: "0000:db:00.0",
								},
							},
						},
						Rank: uint32(0),
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
							Devices: []*ctlpb.SmdDevice{pbFaultDev(0)},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Rank:    1,
						Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
					},
				},
			},
		},
		"list-devices; filter by uuid": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(ranklist.NilRank),
				Uuid:      test.MockUUID(1),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(0)},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbFaultDev(1)},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{},
					{
						Rank:    1,
						Devices: []*ctlpb.SmdDevice{pbFaultDev(1)},
					},
				},
			},
		},
		"list-devices; DAOS Failure": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(ranklist.NilRank),
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Status: int32(daos.Busy),
						},
					},
				},
			},
			expErr: daos.Busy,
		},
		"device-health": {
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(ranklist.NilRank),
				Uuid:             test.MockUUID(1),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(0)},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbFaultDev(1)},
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
						Devices: []*ctlpb.SmdDevice{
							pbDevWithHealth(
								pbFaultDev(1),
								&ctlpb.BioHealthResp{
									Temperature: 1000000,
									TempWarn:    true,
								}),
						},
					},
				},
			},
		},
		"device-health; no uuid in request": {
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(ranklist.NilRank),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(0)},
						},
					},
					{
						Message: &ctlpb.BioHealthResp{
							Temperature: 100000,
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbFaultDev(1)},
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
						Devices: []*ctlpb.SmdDevice{
							pbDevWithHealth(
								pbNormDev(0),
								&ctlpb.BioHealthResp{
									Temperature: 100000,
								}),
						},
					},
					{
						Rank: 1,
						Devices: []*ctlpb.SmdDevice{
							pbDevWithHealth(
								pbFaultDev(1),
								&ctlpb.BioHealthResp{
									Temperature: 1000000,
									TempWarn:    true,
								}),
						},
					},
				},
			},
		},
		"device-health (NEW SMD); skip health collection": {
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(ranklist.NilRank),
				Uuid:             test.MockUUID(1),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(0)},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNewDev(1)},
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
						Rank:    1,
						Devices: []*ctlpb.SmdDevice{pbNewDev(1)},
					},
				},
			},
		},
		"device-health; DAOS Failure": {
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(ranklist.NilRank),
				Uuid:             test.MockUUID(1),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbFaultDev(0)},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbFaultDev(1)},
						},
					},
					{
						Message: &ctlpb.BioHealthResp{
							Status: int32(daos.FreeMemError),
						},
					},
				},
			},
			expErr: daos.FreeMemError,
		},
		"ambiguous UUID": {
			req: &ctlpb.SmdQueryReq{
				Rank: uint32(ranklist.NilRank),
				Uuid: test.MockUUID(),
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
			defer test.ShowBufferOnFailure(t, buf)

			engineCount := len(tc.drpcResps)
			if engineCount == 0 {
				engineCount = 1
			}

			cfg := config.DefaultServer()
			for i := 0; i < engineCount; i++ {
				cfg.Engines = append(cfg.Engines, engine.MockConfig().WithTargetCount(1))
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
				mdc := newMockDrpcClient(cfg)
				srv.getDrpcClientFn = func(s string) drpc.DomainSocketClient {
					return mdc
				}
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

			gotResp, gotErr := svc.SmdQuery(test.Context(t), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestServer_engineDevMap(t *testing.T) {
	e1 := EngineInstance{}
	e2 := EngineInstance{}
	dev1 := devID{uuid: test.MockUUID(1)}
	dev2 := devID{trAddr: test.MockPCIAddr(1)}

	for name, tc := range map[string]struct {
		devs1        []devID
		devs2        []devID
		expMap       engineDevMap
		expFirstDev1 *devID
		expFirstDev2 *devID
	}{
		"simple": {
			devs1: []devID{dev1},
			devs2: []devID{dev2},
			expMap: engineDevMap{
				&e1: devIDMap{dev1.String(): dev1},
				&e2: devIDMap{dev2.String(): dev2},
			},
			expFirstDev1: &dev1,
			expFirstDev2: &dev2,
		},
		"multiple devs": {
			devs2: []devID{dev1, dev2},
			expMap: engineDevMap{
				&e2: devIDMap{dev1.String(): dev1, dev2.String(): dev2},
			},
			expFirstDev2: &dev1,
		},
		"missing dev": {
			devs2: []devID{dev2},
			expMap: engineDevMap{
				&e2: devIDMap{dev2.String(): dev2},
			},
			expFirstDev2: &dev2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			edm := make(engineDevMap)

			for _, id := range tc.devs1 {
				edm.add(&e1, id)
			}
			for _, id := range tc.devs2 {
				edm.add(&e2, id)
			}

			cmpOpts := []cmp.Option{
				cmp.AllowUnexported(devID{}),
			}
			if diff := cmp.Diff(tc.expMap, edm, cmpOpts...); diff != "" {
				t.Fatalf("unexpected map (-want, +got)\n%s\n", diff)
			}
			test.AssertEqual(t, tc.expFirstDev1, edm[&e1].getFirst(), "unexpected first dev1")
			test.AssertEqual(t, tc.expFirstDev2, edm[&e2].getFirst(), "unexpected first dev2")
		})
	}
}

func TestServer_CtlSvc_SmdManage(t *testing.T) {
	pbNormDevNoPciAddr := new(ctlpb.SmdDevice)
	*pbNormDevNoPciAddr = *pbNormDev(1)
	pbNormDevNoPciAddr.Ctrlr.PciAddr = ""
	devManageBusyResp := &ctlpb.DevManageResp{
		Status: int32(daos.Busy),
		Device: pbNormDev(1),
	}

	for name, tc := range map[string]struct {
		setupAP        bool
		req            *ctlpb.SmdManageReq
		junkResp       bool
		drpcResps      map[int][]*mockDrpcResponse
		harnessStopped bool
		ioStopped      bool
		expResp        *ctlpb.SmdManageResp
		expErr         error
	}{
		"harness not started": {
			req:            &ctlpb.SmdManageReq{},
			harnessStopped: true,
			expErr:         FaultHarnessNotStarted,
		},
		"i/o engine not started": {
			req:       &ctlpb.SmdManageReq{},
			ioStopped: true,
			expErr:    FaultDataPlaneNotStarted,
		},
		"missing operation in drpc request": {
			req:    &ctlpb.SmdManageReq{},
			expErr: errors.New("Unrecognized operation"),
		},
		"dev-replace; missing uuid": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{},
				},
			},
			expErr: errors.New("empty id string"),
		},
		"set-faulty operation; missing uuid": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Faulty{
					Faulty: &ctlpb.SetFaultyReq{},
				},
			},
			expErr: errors.New("empty id string"),
		},
		"drpc resp fails": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: test.MockUUID(),
					},
				},
			},
			junkResp: true,
			expErr:   errors.New("unmarshal"),
		},
		"led-manage; invalid uuid": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: "FOO-BAR",
					},
				},
			},
			expErr: errors.New("neither a valid"),
		},
		"led-manage; invalid pci address": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: "0000:00.00.0",
					},
				},
			},
			expErr: errors.New("neither a valid"),
		},
		"led-manage; pci address not of a vmd backing device": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: test.MockUUID() + ",0000:01:00.0",
					},
				},
			},
			expErr: errors.New("neither a valid"),
		},
		"led-manage; valid pci address of vmd backing device": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: test.MockUUID() + ",5d0505:01:00.0",
					},
				},
			},
			expErr: errors.New("no response"),
		},
		"dev-replace; invalid old uuid": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: "FOO-BAR",
						// New UUID format is validated in lib/control.
						NewDevUuid: test.MockUUID(),
					},
				},
			},
			expErr: errors.New("neither a valid"),
		},
		"set-faulty operation; invalid uuid": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Faulty{
					Faulty: &ctlpb.SetFaultyReq{
						Uuid: "FOO-BAR",
					},
				},
			},
			expErr: errors.New("neither a valid"),
		},
		"led-manage; send failure": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: test.MockUUID(),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					&mockDrpcResponse{
						Message: &ctlpb.SmdManageReq{},
						Error:   errors.New("send failure"),
					},
				},
			},
			expErr: errors.New("send failure"),
		},
		"dev-replace; send failure": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					&mockDrpcResponse{
						Message: &ctlpb.SmdManageReq{},
						Error:   errors.New("send failure"),
					},
				},
			},
			expErr: errors.New("send failure"),
		},
		"set-faulty operation; send failure": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Faulty{
					Faulty: &ctlpb.SetFaultyReq{
						Uuid: test.MockUUID(),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					&mockDrpcResponse{
						Message: &ctlpb.SmdManageReq{},
						Error:   errors.New("send failure"),
					},
				},
			},
			expErr: errors.New("send failure"),
		},
		"led-manage; uuid not found": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: test.MockUUID(0),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
				},
			},
			expErr: errors.New("ids requested but not found:"),
		},
		"led-manage; uuid not resolved to pci addr": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: test.MockUUID(1),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDevNoPciAddr},
						},
					},
				},
			},
			expErr: errors.New("not resolved to a PCI addr"),
		},
		"led-manage; ids not found": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids: test.MockUUID(1) + ",d50505:01:00.0",
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentDev(2),
						},
					},
				},
			},
			expErr: errors.New("ids requested but not found: [d50505:01:00.0]"),
		},
		"led-manage": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						// Matches ID returned in initial list query.
						Ids: test.MockUUID(1),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentDev(1),
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentDev(1)},
						},
					},
				},
			},
		},
		"led-manage; dual-engine": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						// Matches ID returned in initial list query.
						Ids: test.MockUUID(2),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentDev(1),
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(2)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentDev(2),
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Rank: 1,
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentDev(2)},
						},
					},
				},
			},
		},
		"led-manage; dual-engine; no ids in request": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					// No ids specified in request should return all.
					Led: &ctlpb.LedManageReq{},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentDev(1),
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(2)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentDev(2),
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentDev(1)},
						},
					},
					{
						Rank: 1,
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentDev(2)},
						},
					},
				},
			},
		},
		"led-manage; mixed id types in request": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						// Matches IDs returned in initial list query.
						Ids: test.MockUUID(1) + ",d50505:01:00.0",
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentDev(1),
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{
								func() *ctlpb.SmdDevice {
									sd := pbNormDev(2)
									sd.Ctrlr.PciAddr = "d50505:01:00.0"
									return sd
								}(),
							},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: func() *ctlpb.SmdDevice {
								sd := pbIdentDev(2)
								sd.Ctrlr.PciAddr = "d50505:01:00.0"
								return sd
							}(),
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentDev(1)},
						},
					},
					{
						Rank: 1,
						Results: []*ctlpb.SmdManageResp_Result{
							{
								Device: func() *ctlpb.SmdDevice {
									sd := pbIdentDev(2)
									sd.Ctrlr.PciAddr = "d50505:01:00.0"
									return sd
								}(),
							},
						},
					},
				},
			},
		},
		// Multiple NVMe namespaces per SSD.
		"led-manage; multiple dev ids for the same traddr": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						// Matches IDs returned in initial list query.
						Ids: test.MockUUID(1) + "," + test.MockUUID(2),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{
								pbNormDev(1),
								func() *ctlpb.SmdDevice {
									d := *pbNormDev(1)
									d.Uuid = test.MockUUID(2)
									return &d
								}(),
							},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentDev(1),
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentDev(1)},
						},
					},
				},
			},
		},
		"set-faulty; uuid not found": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Faulty{
					Faulty: &ctlpb.SetFaultyReq{
						Uuid: test.MockUUID(0),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
				},
			},
			expErr: errors.New("ids requested but not found:"),
		},
		"set-faulty": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Faulty{
					Faulty: &ctlpb.SetFaultyReq{
						Uuid: test.MockUUID(1),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbFaultDev(1),
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{{}},
					},
				},
			},
		},
		"set-faulty; dual-engine": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Faulty{
					Faulty: &ctlpb.SetFaultyReq{
						Uuid: test.MockUUID(1),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{},
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbFaultDev(1),
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Rank:    1,
						Results: []*ctlpb.SmdManageResp_Result{{}},
					},
				},
			},
		},
		"dev-replace; uuid not found": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(0),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
				},
			},
			expErr: errors.New("ids requested but not found:"),
		},
		"dev-replace": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(1),
						NewDevUuid: test.MockUUID(2),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbNormDev(2),
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{{}},
					},
				},
			},
		},
		"dev-replace; dual-engine": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(1),
						NewDevUuid: test.MockUUID(2),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbNormDev(2),
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{},
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{{}},
					},
				},
			},
		},
		"dev-replace; retry on busy": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(1),
						NewDevUuid: test.MockUUID(2),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{Message: devManageBusyResp},
					{Message: devManageBusyResp},
					{Message: &ctlpb.DevManageResp{Device: pbNormDev(2)}},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{{}},
					},
				},
			},
		},
		"dev-replace; retry on busy; other error": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(1),
						NewDevUuid: test.MockUUID(2),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{Message: devManageBusyResp},
					{
						Message: &ctlpb.DevManageResp{
							Status: int32(daos.TimedOut),
							Device: pbNormDev(1),
						},
					},
					{Message: devManageBusyResp},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{
								Status: int32(daos.TimedOut),
							},
						},
					},
				},
			},
		},
		"dev-replace; retry on busy; keeps busy": {
			req: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(1),
						NewDevUuid: test.MockUUID(2),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev(1)},
						},
					},
					{Message: devManageBusyResp},
					{Message: devManageBusyResp},
					{Message: devManageBusyResp},
					{Message: devManageBusyResp},
					{Message: devManageBusyResp},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{
								Status: int32(daos.Busy),
							},
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			origInterval := baseDevReplaceBackoff
			origRetries := maxDevReplaceRetries
			origFactor := maxDevReplaceBackoffFactor
			baseDevReplaceBackoff = 50 * time.Millisecond
			maxDevReplaceRetries = 5
			maxDevReplaceBackoffFactor = 1
			defer func() {
				maxDevReplaceBackoffFactor = origFactor
				maxDevReplaceRetries = origRetries
				baseDevReplaceBackoff = origInterval
			}()

			engineCount := len(tc.drpcResps)
			if engineCount == 0 {
				engineCount = 1
			}

			cfg := config.DefaultServer()
			for i := 0; i < engineCount; i++ {
				cfg.Engines = append(cfg.Engines, engine.MockConfig().WithTargetCount(1))
			}
			svc := mockControlService(t, log, cfg, nil, nil, nil)
			svc.harness.started.SetTrue()

			for i, e := range svc.harness.instances {
				ei := e.(*EngineInstance)
				cfg := new(mockDrpcClientConfig)
				if tc.junkResp {
					cfg.setSendMsgResponse(drpc.Status_SUCCESS, makeBadBytes(42), nil)
				} else if len(tc.drpcResps) > i {
					for _, mock := range tc.drpcResps[i] {
						cfg.setSendMsgResponseList(t, mock)
					}
				}
				mdc := newMockDrpcClient(cfg)
				ei.getDrpcClientFn = func(s string) drpc.DomainSocketClient {
					return mdc
				}
				ei.ready.SetTrue()
			}
			if tc.harnessStopped {
				svc.harness.started.SetFalse()
			}
			if tc.ioStopped {
				for _, srv := range svc.harness.instances {
					srv.(*EngineInstance).ready.SetFalse()
				}
			}

			t.Log(tc.req)
			gotResp, gotErr := svc.SmdManage(test.Context(t), tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, test.DefaultCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got)\n%s\n", diff)
			}
		})
	}
}
