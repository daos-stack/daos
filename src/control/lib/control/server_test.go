//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func Test_SetEngineLogMasks(t *testing.T) {
	for name, tc := range map[string]struct {
		mic         *MockInvokerConfig
		expResponse *SetEngineLogMasksResp
		expErr      error
	}{
		"empty response": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{},
			},
			expResponse: new(SetEngineLogMasksResp),
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
		"bad host addr": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    ",",
							Message: new(ctlpb.SetLogMasksResp),
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
		"invoke fails": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("failed"),
			},
			expErr: errors.New("failed"),
		},
		"server error": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:  "host1",
							Error: errors.New("failed"),
						},
					},
				},
			},
			expResponse: &SetEngineLogMasksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "failed"}),
				HostStorage:    nil,
			},
		},
		"single host; single engine; success": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"",
								},
							},
						},
					},
				},
			},
			expResponse: &SetEngineLogMasksResp{
				HostStorage: MockHostStorageMap(t, &MockStorageScan{
					Hosts:    "host1",
					HostScan: new(ctlpb.StorageScanResp),
				}),
			},
		},
		"single host; multiple engines; success": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"",
									"",
								},
							},
						},
					},
				},
			},
			expResponse: &SetEngineLogMasksResp{
				HostStorage: MockHostStorageMap(t, &MockStorageScan{
					Hosts:    "host1",
					HostScan: new(ctlpb.StorageScanResp),
				}),
			},
		},
		"single host; multiple engines; single engine failure": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"",
									"not ready",
								},
							},
						},
					},
				},
			},
			expResponse: &SetEngineLogMasksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "engine-0: updated, engine-1: not ready"}),
			},
		},
		//		"nvme scan error": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: nvmeFailed,
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "nvme scan failed"}),
		//				HostStorage:    MockHostStorageMap(t, &MockSetLogMasks{"host1", noNvme}),
		//			},
		//		},
		//		"scm and nvme scan error": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: bothFailed,
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t,
		//					&MockHostError{"host1", "nvme scan failed"},
		//					&MockHostError{"host1", "scm scan failed"},
		//				),
		//				HostStorage: MockHostStorageMap(t, &MockSetLogMasks{"host1", noStorage}),
		//			},
		//		},
		//		"no storage": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: noStorage,
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t),
		//				HostStorage:    MockHostStorageMap(t, &MockSetLogMasks{"host1", noStorage}),
		//			},
		//		},
		//		"single host": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: standard,
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t),
		//				HostStorage:    MockHostStorageMap(t, &MockSetLogMasks{"host1", standard}),
		//			},
		//		},
		//		"single host with namespaces": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: pmemA,
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t),
		//				HostStorage:    MockHostStorageMap(t, &MockSetLogMasks{"host1", pmemA}),
		//			},
		//		},
		//		"single host with space utilization": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: withSpaceUsage,
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t),
		//				HostStorage:    MockHostStorageMap(t, &MockSetLogMasks{"host1", withSpaceUsage}),
		//			},
		//		},
		//		"two hosts same scan": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: standard,
		//						},
		//						{
		//							Addr: "host2",
		//							// Use a newly-generated mock here to verify that
		//							// non-hashable fields are ignored.
		//							Message: MockServerScanResp(t, "standard"),
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t),
		//				HostStorage:    MockHostStorageMap(t, &MockSetLogMasks{"host1,host2", standard}),
		//			},
		//		},
		//		"two hosts different scans": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: noNvme,
		//						},
		//						{
		//							Addr:    "host2",
		//							Message: noScm,
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t),
		//				HostStorage: MockHostStorageMap(t,
		//					&MockSetLogMasks{"host1", noNvme},
		//					&MockSetLogMasks{"host2", noScm},
		//				),
		//			},
		//		},
		//		"two hosts different nvme capacity": {
		//			mic: &MockInvokerConfig{
		//				UnaryResponse: &UnaryResponse{
		//					Responses: []*HostResponse{
		//						{
		//							Addr:    "host1",
		//							Message: nvmeBasicA,
		//						},
		//						{
		//							Addr:    "host2",
		//							Message: nvmeBasicB,
		//						},
		//					},
		//				},
		//			},
		//			expResponse: &SetLogMasksResp{
		//				HostErrorsResp: MockHostErrorsResp(t),
		//				HostStorage: MockHostStorageMap(t,
		//					&MockSetLogMasks{"host1", nvmeBasicA},
		//					&MockSetLogMasks{"host2", nvmeBasicB},
		//				),
		//			},
		//		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			ctx := test.Context(t)
			mi := NewMockInvoker(log, tc.mic)

			gotResponse, gotErr := SetEngineLogMasks(ctx, mi, &SetEngineLogMasksReq{})
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResponse, gotResponse, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
