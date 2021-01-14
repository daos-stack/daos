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

package control

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

type mockFabricScan struct {
	Hosts  string
	Fabric *HostFabric
}

func mockHostFabricMap(t *testing.T, scans ...*mockFabricScan) HostFabricMap {
	hfm := make(HostFabricMap)

	for _, scan := range scans {
		hfs := &HostFabricSet{
			HostFabric: scan.Fabric,
			HostSet:    mockHostSet(t, scan.Hosts),
		}

		hk, err := hfs.HostFabric.HashKey()
		if err != nil {
			t.Fatal(err)
		}
		hfm[hk] = hfs
	}

	return hfm
}

func TestControl_NetworkScan(t *testing.T) {
	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		expResp *NetworkScanResp
		expErr  error
	}{
		"local failure": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
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
			expResp: &NetworkScanResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "remote failed"}),
			},
		},
		"bad host addr": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    ",",
							Message: &ctlpb.NetworkScanResp{},
						},
					},
				},
			},
			expErr: errors.New("invalid hostname"),
		},
		"bad host addr with error": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:  ",",
							Error: errors.New("banana"),
						},
					},
				},
			},
			expErr: errors.New("invalid hostname"),
		},
		"nil message": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
						},
					},
				},
			},
			expErr: errors.New("unpack"),
		},
		"one host; one interface": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.NetworkScanResp{
								Interfaces: []*ctlpb.FabricInterface{
									{
										Provider: "test-provider",
										Device:   "test-device",
										Numanode: 42,
									},
								},
							},
						},
					},
				},
			},
			expResp: &NetworkScanResp{
				HostFabrics: mockHostFabricMap(t, &mockFabricScan{
					Hosts: "host1",
					Fabric: &HostFabric{
						Interfaces: []*HostFabricInterface{
							{
								Provider: "test-provider",
								Device:   "test-device",
								NumaNode: 42,
							},
						},
						Providers: []string{"test-provider"},
					},
				}),
			},
		},
		"one host; two interfaces; same provider": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.NetworkScanResp{
								Interfaces: []*ctlpb.FabricInterface{
									{
										Provider: "test-provider",
										Device:   "test-device",
										Numanode: 42,
									},
									{
										Provider: "test-provider",
										Device:   "test-device2",
										Numanode: 84,
									},
								},
							},
						},
					},
				},
			},
			expResp: &NetworkScanResp{
				HostFabrics: mockHostFabricMap(t, &mockFabricScan{
					Hosts: "host1",
					Fabric: &HostFabric{
						Interfaces: []*HostFabricInterface{
							{
								Provider: "test-provider",
								Device:   "test-device",
								NumaNode: 42,
							},
							{
								Provider: "test-provider",
								Device:   "test-device2",
								NumaNode: 84,
							},
						},
						Providers: []string{"test-provider"},
					},
				}),
			},
		},
		"two hosts; same config": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.NetworkScanResp{
								Interfaces: []*ctlpb.FabricInterface{
									{
										Provider: "test-provider",
										Device:   "test-device",
										Numanode: 42,
									},
									{
										Provider: "test-provider",
										Device:   "test-device2",
										Numanode: 84,
									},
								},
							},
						},
						{
							Addr: "host2",
							Message: &ctlpb.NetworkScanResp{
								Interfaces: []*ctlpb.FabricInterface{
									{
										Provider: "test-provider",
										Device:   "test-device",
										Numanode: 42,
									},
									{
										Provider: "test-provider",
										Device:   "test-device2",
										Numanode: 84,
									},
								},
							},
						},
					},
				},
			},
			expResp: &NetworkScanResp{
				HostFabrics: mockHostFabricMap(t, &mockFabricScan{
					Hosts: "host[1-2]",
					Fabric: &HostFabric{
						Interfaces: []*HostFabricInterface{
							{
								Provider: "test-provider",
								Device:   "test-device",
								NumaNode: 42,
							},
							{
								Provider: "test-provider",
								Device:   "test-device2",
								NumaNode: 84,
							},
						},
						Providers: []string{"test-provider"},
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

			gotResp, gotErr := NetworkScan(ctx, mi, &NetworkScanReq{})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_GetAttachInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		mic     *MockInvokerConfig
		req     *GetAttachInfoReq
		expResp *GetAttachInfoResp
		expErr  error
	}{
		"local failure": {
			req: &GetAttachInfoReq{},
			mic: &MockInvokerConfig{
				UnaryError: errors.New("local failed"),
			},
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &GetAttachInfoReq{},
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("host1", errors.New("remote failed"), nil),
			},
			expErr: errors.New("remote failed"),
		},
		"retry after EmptyGroupMap": {
			req: &GetAttachInfoReq{},
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("host1", system.ErrEmptyGroupMap, nil),
					MockMSResponse("host1", nil, &mgmtpb.GetAttachInfoResp{
						RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
							{
								Rank: 42,
								Uri:  "foo://bar",
							},
						},
						Provider: "cow",
					}),
				},
			},
			expResp: &GetAttachInfoResp{
				ServiceRanks: []*PrimaryServiceRank{
					{
						Rank: 42,
						Uri:  "foo://bar",
					},
				},
				Provider: "cow",
			},
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponse: MockMSResponse("", nil, &mgmtpb.GetAttachInfoResp{
					RankUris: []*mgmtpb.GetAttachInfoResp_RankUri{
						{
							Rank: 42,
							Uri:  "foo://bar",
						},
					},
					Provider: "cow",
				}),
			},
			req: &GetAttachInfoReq{},
			expResp: &GetAttachInfoResp{
				ServiceRanks: []*PrimaryServiceRank{
					{
						Rank: 42,
						Uri:  "foo://bar",
					},
				},
				Provider: "cow",
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

			gotResp, gotErr := GetAttachInfo(ctx, mi, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp); diff != "" {
				t.Fatalf("unexpected AttachInfo (-want, +got):\n%s\n", diff)
			}
		})
	}
}
