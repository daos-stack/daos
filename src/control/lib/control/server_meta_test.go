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

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
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
	stateNormal := storage.MockNvmeStateNormal
	stateFaulty := storage.MockNvmeStateEvicted

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
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host-0",
							Message: &ctlpb.SmdQueryResp{
								Ranks: []*ctlpb.SmdQueryResp_RankResp{
									{
										Rank: 0,
										Pools: []*ctlpb.SmdQueryResp_Pool{
											{
												Uuid:   common.MockUUID(0),
												TgtIds: []int32{0, 1},
												Blobs:  []uint64{42, 43},
											},
										},
									},
									{
										Rank: 1,
										Pools: []*ctlpb.SmdQueryResp_Pool{
											{
												Uuid:   common.MockUUID(0),
												TgtIds: []int32{0, 1},
												Blobs:  []uint64{42, 43},
											},
										},
									},
								},
							},
						},
					},
				},
			},
			req: &SmdQueryReq{},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Pools: map[string][]*SmdPool{
							common.MockUUID(0): {
								{
									UUID:      common.MockUUID(0),
									Rank:      system.Rank(0),
									TargetIDs: []int32{0, 1},
									Blobs:     []uint64{42, 43},
								},
								{
									UUID:      common.MockUUID(0),
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
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host-0",
							Message: &ctlpb.SmdQueryResp{
								Ranks: []*ctlpb.SmdQueryResp_RankResp{
									{
										Rank: 0,
										Devices: []*ctlpb.SmdQueryResp_Device{
											{
												Uuid:     common.MockUUID(0),
												TgtIds:   []int32{0},
												DevState: stateNormal.String(),
											},
										},
									},
									{
										Rank: 1,
										Devices: []*ctlpb.SmdQueryResp_Device{
											{
												Uuid:     common.MockUUID(1),
												TgtIds:   []int32{0},
												DevState: stateFaulty.String(),
											},
										},
									},
								},
							},
						},
					},
				},
			},
			req: &SmdQueryReq{},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								UUID:      common.MockUUID(0),
								Rank:      system.Rank(0),
								TargetIDs: []int32{0},
								NvmeState: stateNormal,
							},
							{
								UUID:      common.MockUUID(1),
								Rank:      system.Rank(1),
								TargetIDs: []int32{0},
								NvmeState: stateFaulty,
							},
						},
						Pools: make(map[string][]*SmdPool),
					},
				}),
			},
		},
		"list devices; missing state": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host-0",
							Message: &ctlpb.SmdQueryResp{
								Ranks: []*ctlpb.SmdQueryResp_RankResp{
									{
										Rank: 0,
										Devices: []*ctlpb.SmdQueryResp_Device{
											{
												Uuid:     common.MockUUID(0),
												TgtIds:   []int32{0},
												DevState: "",
											},
										},
									},
								},
							},
						},
					},
				},
			},
			req: &SmdQueryReq{},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								UUID:      common.MockUUID(0),
								Rank:      system.Rank(0),
								TargetIDs: []int32{0},
								NvmeState: storage.NvmeStateUnknown,
							},
						},
						Pools: make(map[string][]*SmdPool),
					},
				}),
			},
		},
		"device health": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host-0",
							Message: &ctlpb.SmdQueryResp{
								Ranks: []*ctlpb.SmdQueryResp_RankResp{
									{
										Rank: 0,
										Devices: []*ctlpb.SmdQueryResp_Device{
											{
												Uuid:   common.MockUUID(0),
												TgtIds: []int32{0},
												Health: &ctlpb.BioHealthResp{
													DevUuid:            common.MockUUID(0),
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
												DevState: stateNormal.String(),
											},
										},
									},
								},
							},
						},
					},
				},
			},
			req: &SmdQueryReq{},
			expResp: &SmdQueryResp{
				HostStorage: mockSmdQueryMap(t, &mockSmdQueryResp{
					Hosts: "host-0",
					SmdInfo: &SmdInfo{
						Devices: []*storage.SmdDevice{
							{
								UUID:      common.MockUUID(0),
								Rank:      system.Rank(0),
								TargetIDs: []int32{0},
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
								NvmeState: stateNormal,
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
			defer common.ShowBufferOnFailure(t, buf)

			mic := tc.mic
			if mic == nil {
				mic = DefaultMockInvokerConfig()
			}

			ctx := context.TODO()
			mi := NewMockInvoker(log, mic)

			gotResp, gotErr := SmdQuery(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				for _, sqr := range gotResp.HostStorage {
					for _, dev := range sqr.HostStorage.SmdInfo.Devices {
						t.Logf("%+v", *dev)
					}
				}
				t.Fatalf("unexpected resp (-want, +got):\n%s\n", diff)
			}
		})
	}
}
