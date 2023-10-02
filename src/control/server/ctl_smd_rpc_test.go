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

	ledStateIdentify = ctlpb.LedState_QUICK_BLINK
	ledStateNormal   = ctlpb.LedState_OFF
	ledStateFault    = ctlpb.LedState_ON
	ledStateUnknown  = ctlpb.LedState_NA
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
									Uuid:     test.MockUUID(0),
									TrAddr:   "0000:8a:00.0",
									TgtIds:   []int32{0, 1, 2},
									DevState: devStateNormal,
									LedState: ledStateNormal,
								},
								{
									Uuid:     test.MockUUID(1),
									TrAddr:   "0000:80:00.0",
									TgtIds:   []int32{3, 4, 5},
									DevState: devStateFaulty,
									LedState: ledStateFault,
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
									Uuid:     test.MockUUID(2),
									TrAddr:   "0000:da:00.0",
									TgtIds:   []int32{0, 1, 2},
									DevState: devStateFaulty,
									LedState: ledStateUnknown,
								},
								{
									Uuid:     test.MockUUID(3),
									TrAddr:   "0000:db:00.0",
									TgtIds:   []int32{3, 4, 5},
									DevState: devStateNormal,
									LedState: ledStateIdentify,
								},
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdQueryResp{
				Ranks: []*ctlpb.SmdQueryResp_RankResp{
					{
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(0),
									TrAddr:   "0000:8a:00.0",
									TgtIds:   []int32{0, 1, 2},
									DevState: devStateNormal,
									LedState: ledStateNormal,
								},
							},
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(1),
									TrAddr:   "0000:80:00.0",
									TgtIds:   []int32{3, 4, 5},
									DevState: devStateFaulty,
									LedState: ledStateFault,
								},
							},
						},
						Rank: uint32(0),
					},
					{
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(2),
									TrAddr:   "0000:da:00.0",
									TgtIds:   []int32{0, 1, 2},
									DevState: devStateFaulty,
									LedState: ledStateUnknown,
								},
							},
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(3),
									TrAddr:   "0000:db:00.0",
									TgtIds:   []int32{3, 4, 5},
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:   test.MockUUID(0),
									TrAddr: "0000:8a:00.0",
									TgtIds: []int32{0, 1, 2},
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
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:     test.MockUUID(0),
									DevState: devStateFaulty,
									LedState: ledStateFault,
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
									Uuid:     test.MockUUID(1),
									DevState: devStateNormal,
									LedState: ledStateNormal,
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(1),
									DevState: devStateNormal,
									LedState: ledStateNormal,
								},
							},
						},
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
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:     test.MockUUID(0),
									DevState: devStateNormal,
									LedState: ledStateNormal,
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
									Uuid:     test.MockUUID(1),
									DevState: devStateFaulty,
									LedState: ledStateFault,
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(1),
									DevState: devStateFaulty,
									LedState: ledStateFault,
								},
							},
						},
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
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid: test.MockUUID(0),
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
									Uuid:     test.MockUUID(1),
									DevState: devStateFaulty,
									LedState: ledStateFault,
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(1),
									DevState: devStateFaulty,
									LedState: ledStateFault,
								},
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
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:     test.MockUUID(0),
									DevState: devStateNormal,
									LedState: ledStateNormal,
								},
							},
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
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:     test.MockUUID(1),
									DevState: devStateFaulty,
									LedState: ledStateFault,
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(0),
									DevState: devStateNormal,
									LedState: ledStateNormal,
								},
								Health: &ctlpb.BioHealthResp{
									Temperature: 100000,
								},
							},
						},
					},
					{
						Rank: 1,
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(1),
									DevState: devStateFaulty,
									LedState: ledStateFault,
								},
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
				Rank:             uint32(ranklist.NilRank),
				Uuid:             test.MockUUID(1),
				IncludeBioHealth: true,
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:     test.MockUUID(0),
									DevState: devStateNew,
									LedState: ledStateNormal,
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
									Uuid:     test.MockUUID(1),
									DevState: devStateNew,
									LedState: ledStateNormal,
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(1),
									DevState: devStateNew,
									LedState: ledStateNormal,
								},
							},
						},
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
							Devices: []*ctlpb.SmdDevice{
								{
									Uuid:     test.MockUUID(0),
									DevState: devStateFaulty,
									LedState: ledStateFault,
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
									Uuid:     test.MockUUID(1),
									DevState: devStateFaulty,
									LedState: ledStateFault,
								},
							},
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
	pbNormDev := &ctlpb.SmdDevice{
		TrAddr:   test.MockPCIAddr(1),
		Uuid:     test.MockUUID(1),
		DevState: devStateNormal,
		LedState: ledStateNormal,
	}
	pbReplacedDev := &ctlpb.SmdDevice{
		TrAddr:   test.MockPCIAddr(2),
		Uuid:     test.MockUUID(2),
		DevState: devStateNormal,
		LedState: ledStateNormal,
	}
	pbNormDevNoTrAddr := new(ctlpb.SmdDevice)
	*pbNormDevNoTrAddr = *pbNormDev
	pbNormDevNoTrAddr.TrAddr = ""
	pbFaultyDev := &ctlpb.SmdDevice{
		TrAddr:   test.MockPCIAddr(1),
		Uuid:     test.MockUUID(1),
		DevState: devStateFaulty,
		LedState: ledStateFault,
	}
	pbIdentifyDev := &ctlpb.SmdDevice{
		TrAddr:   test.MockPCIAddr(1),
		Uuid:     test.MockUUID(1),
		LedState: ledStateIdentify,
	}
	devManageBusyResp := &ctlpb.DevManageResp{Status: int32(daos.Busy), Device: pbNormDev}

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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
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
							Devices: []*ctlpb.SmdDevice{pbNormDevNoTrAddr},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentifyDev,
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentifyDev,
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentifyDev},
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
						Ids: test.MockUUID(1),
					},
				},
			},
			drpcResps: map[int][]*mockDrpcResponse{
				0: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentifyDev,
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
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentifyDev},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentifyDev,
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
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentifyDev},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentifyDev,
						},
					},
				},
				1: {
					{
						Message: &ctlpb.SmdDevResp{
							Devices: []*ctlpb.SmdDevice{
								{
									TrAddr:   "d50505:01:00.0",
									Uuid:     test.MockUUID(2),
									LedState: ledStateNormal,
								},
							},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: &ctlpb.SmdDevice{
								TrAddr:   "d50505:01:00.0",
								Uuid:     test.MockUUID(2),
								LedState: ledStateNormal,
							},
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentifyDev},
						},
					},
					{
						Rank: 1,
						Results: []*ctlpb.SmdManageResp_Result{
							{
								Device: &ctlpb.SmdDevice{
									TrAddr:   "d50505:01:00.0",
									Uuid:     test.MockUUID(2),
									LedState: ledStateNormal,
								},
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
								pbNormDev,
								func() *ctlpb.SmdDevice {
									d := *pbNormDev
									d.Uuid = test.MockUUID(2)
									return &d
								}(),
							},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbIdentifyDev,
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbIdentifyDev},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbFaultyDev,
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbFaultyDev},
						},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbFaultyDev,
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Rank: 1,
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbFaultyDev},
						},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbReplacedDev,
						},
					},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbReplacedDev},
						},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{
						Message: &ctlpb.DevManageResp{
							Device: pbReplacedDev,
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
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbReplacedDev},
						},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{Message: devManageBusyResp},
					{Message: devManageBusyResp},
					{Message: &ctlpb.DevManageResp{Device: pbReplacedDev}},
				},
			},
			expResp: &ctlpb.SmdManageResp{
				Ranks: []*ctlpb.SmdManageResp_RankResp{
					{
						Results: []*ctlpb.SmdManageResp_Result{
							{Device: pbReplacedDev},
						},
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
						},
					},
					{Message: devManageBusyResp},
					{
						Message: &ctlpb.DevManageResp{
							Status: int32(daos.TimedOut),
							Device: pbNormDev,
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
								Device: pbNormDev,
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
							Devices: []*ctlpb.SmdDevice{pbNormDev},
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
								Device: pbNormDev,
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
