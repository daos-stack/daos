//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

type mockSmdQueryResp struct {
	Hosts   string
	SmdInfo *SmdInfo
}

func mockSmdQueryMap(t *testing.T, mocks ...*mockSmdQueryResp) HostStorageMap {
	hsm := make(HostStorageMap)

	for _, mock := range mocks {
		hss := &HostStorageSet{
			HostSet: mockHostSet(t, mock.Hosts),
			HostStorage: &HostStorage{
				SmdInfo: mock.SmdInfo,
			},
		}
		hk, err := hss.HostStorage.HashKey()
		if err != nil {
			t.Fatal(err)
		}
		hsm[hk] = hss
	}

	return hsm
}

func TestControl_SmdQuery(t *testing.T) {
	devStateNew := ctlpb.NvmeDevState_NEW
	devStateNormal := ctlpb.NvmeDevState_NORMAL
	devStateFaulty := ctlpb.NvmeDevState_EVICTED

	ledStateIdentify := ctlpb.VmdLedState_QUICK_BLINK
	ledStateNormal := ctlpb.VmdLedState_OFF
	ledStateFault := ctlpb.VmdLedState_ON
	ledStateUnknown := ctlpb.VmdLedState_NA

	newMockInvokerWRankResps := func(rankResps ...*ctlpb.SmdQueryResp_RankResp) *MockInvokerConfig {
		return &MockInvokerConfig{
			UnaryResponse: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr: "host-0",
						Message: &ctlpb.SmdQueryResp{
							Ranks: rankResps,
						},
					},
				},
			},
		}
	}

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *SmdQueryReq
		expResp *SmdQueryResp
		expErr  error
	}{
		"local failure": {
			req: &SmdQueryReq{},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &SmdQueryReq{},
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:  "host1",
							Error: errors.New("remote failed"),
						},
					},
				},
			},
			expResp: &SmdQueryResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "remote failed"}),
			},
		},
		"nil request": {
			req:    nil,
			expErr: errors.New("nil request"),
		},
		"invalid UUID": {
			req: &SmdQueryReq{
				UUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"set-faulty with > 1 host": {
			req: &SmdQueryReq{
				unaryRequest: unaryRequest{
					request: request{
						HostList: mockHostList("one", "two"),
					},
				},
				SetFaulty: true,
			},
			expErr: errors.New("> 1 host"),
		},
		"list pools": {
			mic: newMockInvokerWRankResps([]*ctlpb.SmdQueryResp_RankResp{
				{
					Rank: 0,
					Pools: []*ctlpb.SmdQueryResp_Pool{
						{
							Uuid:   test.MockUUID(0),
							TgtIds: []int32{0, 1},
							Blobs:  []uint64{42, 43},
						},
					},
				},
				{
					Rank: 1,
					Pools: []*ctlpb.SmdQueryResp_Pool{
						{
							Uuid:   test.MockUUID(0),
							TgtIds: []int32{0, 1},
							Blobs:  []uint64{42, 43},
						},
					},
				},
			}...),
			req: &SmdQueryReq{},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Pools: map[string][]*SmdPool{
							test.MockUUID(0): {
								{
									UUID:      test.MockUUID(0),
									Rank:      system.Rank(0),
									TargetIDs: []int32{0, 1},
									Blobs:     []uint64{42, 43},
								},
								{
									UUID:      test.MockUUID(0),
									Rank:      system.Rank(1),
									TargetIDs: []int32{0, 1},
									Blobs:     []uint64{42, 43},
								},
							},
						},
					},
				}),
			},
		},
		"list devices": {
			mic: newMockInvokerWRankResps([]*ctlpb.SmdQueryResp_RankResp{
				{
					Rank: 0,
					Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
						{
							Details: &ctlpb.SmdDevice{
								TrAddr:   test.MockPCIAddr(1),
								Uuid:     test.MockUUID(0),
								TgtIds:   []int32{0},
								DevState: devStateNormal,
								LedState: ledStateNormal,
							},
						},
					},
				},
				{
					Rank: 1,
					Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
						{
							Details: &ctlpb.SmdDevice{
								TrAddr:   test.MockPCIAddr(1),
								Uuid:     test.MockUUID(1),
								TgtIds:   []int32{0},
								DevState: devStateFaulty,
								LedState: ledStateFault,
							},
						},
					},
				},
			}...),
			req: &SmdQueryReq{},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(0),
								Rank:      system.Rank(0),
								TargetIDs: []int32{0},
								NvmeState: storage.NvmeStateNormal,
								LedState:  storage.LedStateNormal,
							},
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(1),
								Rank:      system.Rank(1),
								TargetIDs: []int32{0},
								NvmeState: storage.NvmeStateFaulty,
								LedState:  storage.LedStateFaulty,
							},
						},
						Pools: make(map[string][]*SmdPool),
					},
				}),
			},
		},
		"list devices; missing led state": {
			mic: newMockInvokerWRankResps(&ctlpb.SmdQueryResp_RankResp{
				Rank: 0,
				Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
					{
						Details: &ctlpb.SmdDevice{
							TrAddr:   test.MockPCIAddr(2),
							Uuid:     test.MockUUID(1),
							TgtIds:   []int32{1, 2, 3},
							DevState: devStateNew,
							LedState: ledStateUnknown,
						},
					},
				},
			}),
			req: &SmdQueryReq{},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(2),
								UUID:      test.MockUUID(1),
								Rank:      system.Rank(0),
								TargetIDs: []int32{1, 2, 3},
								NvmeState: storage.NvmeStateNew,
								LedState:  storage.LedStateUnknown,
							},
						},
						Pools: make(map[string][]*SmdPool),
					},
				}),
			},
		},
		"list devices; show only faulty": {
			mic: newMockInvokerWRankResps(
				&ctlpb.SmdQueryResp_RankResp{
					Rank: 1,
					Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
						{
							Details: &ctlpb.SmdDevice{
								TrAddr:   test.MockPCIAddr(1),
								Uuid:     test.MockUUID(1),
								TgtIds:   []int32{1, 2, 3},
								DevState: devStateFaulty,
								LedState: ledStateUnknown,
							},
						},
					},
				},
				&ctlpb.SmdQueryResp_RankResp{
					Rank: 0,
					Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
						{
							Details: &ctlpb.SmdDevice{
								TrAddr:   test.MockPCIAddr(2),
								Uuid:     test.MockUUID(3),
								TgtIds:   []int32{4, 5, 6},
								DevState: devStateNormal,
								LedState: ledStateUnknown,
							},
						},
					},
				},
			),
			req: &SmdQueryReq{FaultyDevsOnly: true},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(1),
								Rank:      system.Rank(1),
								TargetIDs: []int32{1, 2, 3},
								NvmeState: storage.NvmeStateFaulty,
								LedState:  storage.LedStateUnknown,
							},
						},
						Pools: make(map[string][]*SmdPool),
					},
				}),
			},
		},
		"device health": {
			mic: newMockInvokerWRankResps(&ctlpb.SmdQueryResp_RankResp{
				Rank: 0,
				Devices: []*ctlpb.SmdQueryResp_SmdDeviceWithHealth{
					{
						Details: &ctlpb.SmdDevice{
							TrAddr:   test.MockPCIAddr(1),
							Uuid:     test.MockUUID(1),
							TgtIds:   []int32{1, 2, 3},
							LedState: ledStateIdentify,
							DevState: devStateNormal,
						},
						Health: &ctlpb.BioHealthResp{
							DevUuid:            test.MockUUID(1),
							Temperature:        2,
							MediaErrs:          3,
							BioReadErrs:        4,
							BioWriteErrs:       5,
							BioUnmapErrs:       6,
							ChecksumErrs:       7,
							TempWarn:           true,
							AvailSpareWarn:     true,
							ReadOnlyWarn:       true,
							DevReliabilityWarn: true,
							VolatileMemWarn:    true,
						},
					},
				},
			}),
			req: &SmdQueryReq{},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(1),
								Rank:      system.Rank(0),
								TargetIDs: []int32{1, 2, 3},
								NvmeState: storage.NvmeStateNormal,
								LedState:  storage.LedStateIdentify,
								Health: &storage.NvmeHealth{
									Temperature:     2,
									MediaErrors:     3,
									ReadErrors:      4,
									WriteErrors:     5,
									UnmapErrors:     6,
									ChecksumErrors:  7,
									TempWarn:        true,
									AvailSpareWarn:  true,
									ReadOnlyWarn:    true,
									ReliabilityWarn: true,
									VolatileWarn:    true,
								},
							},
						},
						Pools: make(map[string][]*SmdPool),
					},
				}),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := SmdQuery(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				// Display SmdDevice differences in a readable manner.
				for _, sqr := range gotResp.HostStorage {
					for i, gotDev := range sqr.HostStorage.SmdInfo.Devices {
						hs := tc.expResp.HostStorage
						expDev := hs[hs.Keys()[0]].HostStorage.SmdInfo.Devices[i]
						t.Logf(cmp.Diff(expDev, gotDev, defResCmpOpts()...))
					}
				}
				t.Fatalf("unexpected resp (-want, +got):\n%s\n", diff)
			}
		})
	}
}
