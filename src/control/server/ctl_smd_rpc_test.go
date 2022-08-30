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

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_CtlSvc_SmdQuery(t *testing.T) {
	devStateUnplugged := storage.NvmeDevState(0).String()
	devStateNew := storage.NvmeStateNew.String()
	devStateNormal := storage.NvmeStateNormal.String()
	devStateFaulty := storage.NvmeStateFaulty.String()
	ledStateIdentify := ctlpb.VmdLedState_QUICK_BLINK
	ledStateNormal := ctlpb.VmdLedState_OFF
	ledStateFault := ctlpb.VmdLedState_ON
	ledStateUnknown := ctlpb.VmdLedState_NA

	pbNormDev := &ctlpb.SmdDevice{
		Uuid:     test.MockUUID(),
		DevState: devStateNormal,
		LedState: ledStateNormal,
	}
	pbFaultyQueryDev := &ctlpb.SmdDevice{
		Uuid:     test.MockUUID(),
		DevState: devStateFaulty,
		LedState: ledStateFault,
	}
	pbIdentifyQueryDev := &ctlpb.SmdDevice{
		Uuid:     test.MockUUID(),
		DevState: devStateNormal,
		LedState: ledStateIdentify,
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
				Uuid:      test.MockUUID(),
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
							Results: []*ctlpb.DevManageResp_Result{
								{
									Device: &ctlpb.SmdDevice{
										Uuid:     test.MockUUID(),
										DevState: devStateFaulty,
										LedState: ledStateFault,
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{Details: pbFaultyQueryDev},
						},
					},
				},
			},
		},
		"set-faulty; DAOS Failure": {
			req: &ctlpb.SmdQueryReq{
				SetFaulty: true,
				Uuid:      test.MockUUID(),
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
							Status: int32(daos.InvalidInput),
						},
					},
				},
			},
			expErr: daos.InvalidInput,
		},
		"identify": {
			req: &ctlpb.SmdQueryReq{
				Identify: true,
				Uuid:     test.MockUUID(),
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
							Results: []*ctlpb.DevManageResp_Result{
								{
									Device: &ctlpb.SmdDevice{
										Uuid:     test.MockUUID(),
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{Details: pbIdentifyQueryDev},
						},
					},
				},
			},
		},
		"get-led": {
			req: &ctlpb.SmdQueryReq{
				GetLed: true,
				Uuid:   test.MockUUID(),
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
							Results: []*ctlpb.DevManageResp_Result{
								{
									Device: &ctlpb.SmdDevice{
										Uuid:     test.MockUUID(),
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{Details: pbIdentifyQueryDev},
						},
					},
				},
			},
		},
		"reset-led": {
			req: &ctlpb.SmdQueryReq{
				ResetLed: true,
				Uuid:     test.MockUUID(),
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
							Results: []*ctlpb.DevManageResp_Result{
								{
									Device: &ctlpb.SmdDevice{
										Uuid:     test.MockUUID(),
										DevState: devStateNormal,
										LedState: ledStateNormal,
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
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{Details: pbNormDev},
						},
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
				Rank:        uint32(system.NilRank),
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
				Rank:        uint32(system.NilRank),
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
				Rank:      uint32(system.NilRank),
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
									DevState: devStateUnplugged,
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
									DevState: devStateUnplugged,
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
				Rank:      uint32(system.NilRank),
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
		"list-devices; show only faulty": {
			req: &ctlpb.SmdQueryReq{
				OmitPools: true,
				Rank:      uint32(system.NilRank),
				StateMask: storage.NvmeFlagFaulty.Uint32(),
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
									TrAddr:   "0000:8b:00.0",
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
									DevState: devStateUnplugged,
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
									Uuid:     test.MockUUID(1),
									TrAddr:   "0000:8b:00.0",
									TgtIds:   []int32{3, 4, 5},
									DevState: devStateFaulty,
									LedState: ledStateFault,
								},
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
				Rank:      uint32(system.NilRank),
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
				Rank:      uint32(system.NilRank),
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
				Rank:             uint32(system.NilRank),
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
		"device-health (NEW SMD); skip health collection": {
			req: &ctlpb.SmdQueryReq{
				OmitPools:        true,
				Rank:             uint32(system.NilRank),
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
				Rank:             uint32(system.NilRank),
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
									TgtIds:   []int32{0},
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
						Rank: 1,
						Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
							{
								Details: &ctlpb.SmdDevice{
									Uuid:     test.MockUUID(1),
									TgtIds:   []int32{0},
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
