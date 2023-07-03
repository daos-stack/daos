//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func Test_setLogMasksReqToPB(t *testing.T) {
	masks := "ERR,mgmt=DEBUG"
	badMasks := "ERR,mgmt=DEBUGX"
	streams := "EPC,MEM"
	badStreams := "EPC,MEMX"
	subsystems := "hg,lm"
	badSubsystems := "hg,lmx"

	for name, tc := range map[string]struct {
		inReq     *SetEngineLogMasksReq
		expOutReq *ctlpb.SetLogMasksReq
		expErr    error
	}{
		"reset all fields": {
			inReq: &SetEngineLogMasksReq{},
			expOutReq: &ctlpb.SetLogMasksReq{
				ResetMasks:      true,
				ResetStreams:    true,
				ResetSubsystems: true,
			},
		},
		"set all fields": {
			inReq: &SetEngineLogMasksReq{
				Masks:      &masks,
				Streams:    &streams,
				Subsystems: &subsystems,
			},
			expOutReq: &ctlpb.SetLogMasksReq{
				Masks:      masks,
				Streams:    streams,
				Subsystems: subsystems,
			},
		},
		"bad masks": {
			inReq: &SetEngineLogMasksReq{
				Masks:      &badMasks,
				Streams:    &streams,
				Subsystems: &subsystems,
			},
			expErr: errors.New("unknown log level"),
		},
		"bad streams": {
			inReq: &SetEngineLogMasksReq{
				Masks:      &masks,
				Streams:    &badStreams,
				Subsystems: &subsystems,
			},
			expErr: errors.New("unknown name"),
		},
		"bad subsystems": {
			inReq: &SetEngineLogMasksReq{
				Masks:      &masks,
				Streams:    &streams,
				Subsystems: &badSubsystems,
			},
			expErr: errors.New("unknown name"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOutReq, gotErr := setLogMasksReqToPB(tc.inReq)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			opt := cmpopts.IgnoreUnexported(ctlpb.SetLogMasksReq{})
			if diff := cmp.Diff(tc.expOutReq, gotOutReq, opt); diff != "" {
				t.Fatalf("unexpected pb request (-want, +got):\n%s\n", diff)
			}
		})
	}
}

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
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{
					Hosts: "host1",
					Error: "failed",
				}),
				HostStorage: nil,
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
		"single host; single engine; failure": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"not ready",
								},
							},
						},
					},
				},
			},
			expResponse: &SetEngineLogMasksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{
					Hosts: "host1",
					Error: "engine-0: not ready",
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
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{
					Hosts: "host1",
					Error: "engine-0: updated, engine-1: not ready",
				}),
			},
		},
		"single host; multiple engines; all engines fail": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"drpc fails",
									"not ready",
								},
							},
						},
					},
				},
			},
			expResponse: &SetEngineLogMasksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{
					Hosts: "host1",
					Error: "engine-0: drpc fails, engine-1: not ready",
				}),
			},
		},
		"multiple hosts; multiple engines; all engines fail": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"drpc fails",
									"not ready",
								},
							},
						},
						{
							Addr: "host2",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"drpc fails",
									"not ready",
								},
							},
						},
					},
				},
			},
			expResponse: &SetEngineLogMasksResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{
					Hosts: "host[1-2]",
					Error: "engine-0: drpc fails, engine-1: not ready",
				}),
			},
		},
		"multiple hosts; multiple engines; most engines fail": {
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
						{
							Addr: "host2",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"drpc fails",
									"not ready",
								},
							},
						},
					},
				},
			},
			expResponse: &SetEngineLogMasksResp{
				HostErrorsResp: MockHostErrorsResp(t,
					&MockHostError{
						Hosts: "host1",
						Error: "engine-0: updated, engine-1: not ready",
					},
					&MockHostError{
						Hosts: "host2",
						Error: "engine-0: drpc fails, engine-1: not ready",
					}),
			},
		},
		"multiple hosts; multiple engines; most engines succeed": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.SetLogMasksResp{
								Errors: []string{
									"drpc fails",
									"",
								},
							},
						},
						{
							Addr: "host2",
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
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{
					Hosts: "host1",
					Error: "engine-0: drpc fails, engine-1: updated",
				}),
				HostStorage: MockHostStorageMap(t, &MockStorageScan{
					Hosts:    "host2",
					HostScan: new(ctlpb.StorageScanResp),
				}),
			},
		},
		"multiple hosts; multiple engines; all engines succeed": {
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
						{
							Addr: "host2",
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
					Hosts:    "host[1-2]",
					HostScan: new(ctlpb.StorageScanResp),
				}),
			},
		},
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
