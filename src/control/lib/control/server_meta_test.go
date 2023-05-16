//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type mockSmdResp struct {
	Hosts   string
	SmdInfo *SmdInfo
}

func mockSmdQueryMap(t *testing.T, mocks ...*mockSmdResp) HostStorageMap {
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

	ledStateIdentify := ctlpb.LedState_QUICK_BLINK
	ledStateNormal := ctlpb.LedState_OFF
	ledStateFault := ctlpb.LedState_ON
	ledStateUnknown := ctlpb.LedState_NA

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
		expResp *SmdResp
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
			expResp: &SmdResp{
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
			expResp: &SmdResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Pools: map[string][]*SmdPool{
							test.MockUUID(0): {
								{
									UUID:      test.MockUUID(0),
									Rank:      ranklist.Rank(0),
									TargetIDs: []int32{0, 1},
									Blobs:     []uint64{42, 43},
								},
								{
									UUID:      test.MockUUID(0),
									Rank:      ranklist.Rank(1),
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
								TgtIds:   []int32{1024, 1, 1, 2, 2, 3, 3},
								DevState: devStateNormal,
								LedState: ledStateNormal,
								RoleBits: storage.BdevRoleAll,
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
								RoleBits: storage.BdevRoleData,
							},
						},
					},
				},
			}...),
			req: &SmdQueryReq{},
			expResp: &SmdResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(0),
								Rank:      ranklist.Rank(0),
								TargetIDs: []int32{1, 2, 3},
								NvmeState: storage.NvmeStateNormal,
								LedState:  storage.LedStateNormal,
								Roles: storage.BdevRoles{
									storage.OptionBits(storage.BdevRoleAll),
								},
								HasSysXS: true,
							},
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(1),
								Rank:      ranklist.Rank(1),
								TargetIDs: []int32{0},
								NvmeState: storage.NvmeStateFaulty,
								LedState:  storage.LedStateFaulty,
								Roles: storage.BdevRoles{
									storage.OptionBits(storage.BdevRoleData),
								},
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
			expResp: &SmdResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(2),
								Rank:      ranklist.Rank(0),
								TargetIDs: []int32{1, 2, 3},
								UUID:      test.MockUUID(1),
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
			expResp: &SmdResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(1),
								Rank:      ranklist.Rank(1),
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
			expResp: &SmdResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(1),
								Rank:      ranklist.Rank(0),
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

			ctx := test.Context(t)
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

func TestControl_packPBSmdManageReq(t *testing.T) {
	for name, tc := range map[string]struct {
		req      *SmdManageReq
		expPBReq *ctlpb.SmdManageReq
		expErr   error
	}{
		"no operation in request": {
			req:    &SmdManageReq{},
			expErr: errors.New("unrecognized operation requested"),
		},
		"bad operation in request": {
			req: &SmdManageReq{
				Operation: SmdManageOpcode(99),
				IDs:       test.MockUUID(),
			},
			expErr: errors.New("unrecognized operation requested"),
		},
		"invalid UUID": {
			req: &SmdManageReq{
				Operation: SetFaultyOp,
				IDs:       "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"set-faulty": {
			req: &SmdManageReq{
				Operation: SetFaultyOp,
				IDs:       test.MockUUID(1),
			},
			expPBReq: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Faulty{
					Faulty: &ctlpb.SetFaultyReq{
						Uuid: test.MockUUID(1),
					},
				},
			},
		},
		"dev-replace; missing old device uuid": {
			req: &SmdManageReq{
				Operation:   DevReplaceOp,
				ReplaceUUID: test.MockUUID(1),
			},
			expErr: errors.New("invalid UUID"),
		},
		"dev-replace; bad new device uuid": {
			req: &SmdManageReq{
				Operation:   DevReplaceOp,
				IDs:         test.MockUUID(1),
				ReplaceUUID: "bad",
			},
			expErr: errors.New("invalid UUID"),
		},
		"dev-replace; same old and new device uuids": {
			req: &SmdManageReq{
				Operation:   DevReplaceOp,
				IDs:         test.MockUUID(1),
				ReplaceUUID: test.MockUUID(1),
			},
			expPBReq: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(1),
						NewDevUuid: test.MockUUID(1),
					},
				},
			},
		},
		"dev-replace": {
			req: &SmdManageReq{
				Operation:      DevReplaceOp,
				IDs:            test.MockUUID(1),
				ReplaceUUID:    test.MockUUID(2),
				ReplaceNoReint: true,
			},
			expPBReq: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Replace{
					Replace: &ctlpb.DevReplaceReq{
						OldDevUuid: test.MockUUID(1),
						NewDevUuid: test.MockUUID(2),
						NoReint:    true,
					},
				},
			},
		},
		"led-manage; get status": {
			req: &SmdManageReq{
				Operation: LedCheckOp,
				IDs:       fmt.Sprintf(test.MockUUID(1), test.MockPCIAddr(1)),
			},
			expPBReq: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids:       fmt.Sprintf(test.MockUUID(1), test.MockPCIAddr(1)),
						LedState:  ctlpb.LedState_NA,
						LedAction: ctlpb.LedAction_GET,
					},
				},
			},
		},
		"led-manage; identify": {
			req: &SmdManageReq{
				Operation: LedBlinkOp,
				IDs:       fmt.Sprintf(test.MockUUID(1), test.MockPCIAddr(1)),
			},
			expPBReq: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids:       fmt.Sprintf(test.MockUUID(1), test.MockPCIAddr(1)),
						LedState:  ctlpb.LedState_QUICK_BLINK,
						LedAction: ctlpb.LedAction_SET,
					},
				},
			},
		},
		"led-manage; reset": {
			req: &SmdManageReq{
				Operation: LedResetOp,
				IDs:       fmt.Sprintf(test.MockUUID(1), test.MockPCIAddr(1)),
			},
			expPBReq: &ctlpb.SmdManageReq{
				Op: &ctlpb.SmdManageReq_Led{
					Led: &ctlpb.LedManageReq{
						Ids:       fmt.Sprintf(test.MockUUID(1), test.MockPCIAddr(1)),
						LedState:  ctlpb.LedState_NA,
						LedAction: ctlpb.LedAction_RESET,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			pbReq := new(ctlpb.SmdManageReq)
			gotErr := packPBSmdManageReq(tc.req, pbReq)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := append(defResCmpOpts(),
				cmpopts.IgnoreUnexported(ctlpb.SmdManageReq{}, ctlpb.SetFaultyReq{},
					ctlpb.DevReplaceReq{}, ctlpb.LedManageReq{}))

			if diff := cmp.Diff(tc.expPBReq, pbReq, cmpOpts...); diff != "" {
				t.Fatalf("unexpected resp (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_SmdManage(t *testing.T) {
	devStateNormal := ctlpb.NvmeDevState_NORMAL
	ledStateIdentify := ctlpb.LedState_QUICK_BLINK

	newMockInvokerWRankResps := func(rankResps ...*ctlpb.SmdManageResp_RankResp) *MockInvokerConfig {
		return &MockInvokerConfig{
			UnaryResponse: &UnaryResponse{
				Responses: []*HostResponse{
					{
						Addr: "host-0",
						Message: &ctlpb.SmdManageResp{
							Ranks: rankResps,
						},
					},
				},
			},
		}
	}

	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *SmdManageReq
		expResp *SmdResp
		expErr  error
	}{
		"nil request": {
			req:    nil,
			expErr: errors.New("nil request"),
		},
		"local failure": {
			req: &SmdManageReq{
				Operation: SetFaultyOp,
				IDs:       test.MockUUID(),
			},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &SmdManageReq{
				Operation: SetFaultyOp,
				IDs:       test.MockUUID(),
			},
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
			expResp: &SmdResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "remote failed"}),
			},
		},
		"set-faulty with > 1 host": {
			req: &SmdManageReq{
				unaryRequest: unaryRequest{
					request: request{
						HostList: mockHostList("one", "two"),
					},
				},
				Operation: SetFaultyOp,
				IDs:       test.MockUUID(),
			},
			expErr: errors.New("> 1 host"),
		},
		"set-faulty": {
			req: &SmdManageReq{
				Operation: SetFaultyOp,
				IDs:       test.MockUUID(1),
			},
			mic: newMockInvokerWRankResps(&ctlpb.SmdManageResp_RankResp{
				Rank: 0,
				Results: []*ctlpb.SmdManageResp_Result{
					{
						Device: &ctlpb.SmdDevice{
							TrAddr:   test.MockPCIAddr(1),
							Uuid:     test.MockUUID(1),
							TgtIds:   []int32{1024, 1, 1, 2, 2, 3, 3},
							LedState: ledStateIdentify,
							DevState: devStateNormal,
							RoleBits: storage.BdevRoleAll,
						},
					},
				},
			}),
			expResp: &SmdResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(1),
								UUID:      test.MockUUID(1),
								Rank:      ranklist.Rank(0),
								TargetIDs: []int32{1, 2, 3},
								NvmeState: storage.NvmeStateNormal,
								LedState:  storage.LedStateIdentify,
								Roles: storage.BdevRoles{
									storage.OptionBits(storage.BdevRoleAll),
								},
								HasSysXS: true,
							},
						},
					},
				}),
			},
		},
		"set-faulty; drpc failure": {
			req: &SmdManageReq{
				Operation: SetFaultyOp,
				IDs:       test.MockUUID(1),
			},
			mic: newMockInvokerWRankResps(&ctlpb.SmdManageResp_RankResp{
				Rank: 0,
				Results: []*ctlpb.SmdManageResp_Result{
					{
						Status: int32(daos.Busy),
						Device: &ctlpb.SmdDevice{
							TrAddr: test.MockPCIAddr(1),
						},
					},
				},
			}),
			expResp: &SmdResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{
					Hosts: "host-0",
					Error: fmt.Sprintf("rank 0: %s DER_BUSY(-1012): Device or resource busy",
						test.MockPCIAddr(1)),
				}),
				HostStorage: mockSmdQueryMap(t, &mockSmdResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								TrAddr:    test.MockPCIAddr(1),
								TargetIDs: []int32{},
							},
						},
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

			ctx := test.Context(t)
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := SmdManage(ctx, mi, tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				// Display SmdDevice differences in a readable manner.
				for _, sqr := range gotResp.HostStorage {
					hs := tc.expResp.HostStorage
					keys := hs.Keys()
					if len(keys) == 0 {
						continue
					}
					for i, gotDev := range sqr.HostStorage.SmdInfo.Devices {
						expDev := hs[keys[0]].HostStorage.SmdInfo.Devices[i]
						t.Logf(cmp.Diff(expDev, gotDev, defResCmpOpts()...))
					}
				}
				t.Fatalf("unexpected resp (-want, +got):\n%s\n", diff)
			}
		})
	}
}
