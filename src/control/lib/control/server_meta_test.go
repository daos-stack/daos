//
// (C) Copyright 2020-2021 Intel Corporation.
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
												Uuid:   common.MockUUID(0),
												TgtIds: []int32{0},
											},
										},
									},
									{
										Rank: 1,
										Devices: []*ctlpb.SmdQueryResp_Device{
											{
												Uuid:   common.MockUUID(1),
												TgtIds: []int32{0},
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
							},
							{
								UUID:      common.MockUUID(1),
								Rank:      system.Rank(1),
								TargetIDs: []int32{0},
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
				t.Fatalf("unexpected resp (-want, +got):\n%s\n", diff)
			}
		})
	}
}
