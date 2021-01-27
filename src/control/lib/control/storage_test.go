//
// (C) Copyright 2020-2021 Intel Corporation.
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
)

func TestControl_StorageScan(t *testing.T) {
	var (
		standardScan       = MockServerScanResp(t, "standard")
		withNamespacesScan = MockServerScanResp(t, "withNamespace")
		withSpaceUsageScan = MockServerScanResp(t, "withSpaceUsage")
		noNVMEScan         = MockServerScanResp(t, "noNVME")
		noSCMScan          = MockServerScanResp(t, "noSCM")
		noStorageScan      = MockServerScanResp(t, "noStorage")
		scmScanFailed      = MockServerScanResp(t, "scmFailed")
		nvmeScanFailed     = MockServerScanResp(t, "nvmeFailed")
		bothScansFailed    = MockServerScanResp(t, "bothFailed")
	)
	for name, tc := range map[string]struct {
		mic         *MockInvokerConfig
		expResponse *StorageScanResp
		expErr      error
	}{
		"empty response": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{},
			},
			expResponse: new(StorageScanResp),
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
							Message: standardScan,
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
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "failed"}),
				HostStorage:    nil,
			},
		},
		"scm scan error": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: scmScanFailed,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "scm scan failed"}),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", scmScanFailed}),
			},
		},
		"nvme scan error": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: nvmeScanFailed,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "nvme scan failed"}),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", noNVMEScan}),
			},
		},
		"scm and nvme scan error": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: bothScansFailed,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t,
					&MockHostError{"host1", "nvme scan failed"},
					&MockHostError{"host1", "scm scan failed"},
				),
				HostStorage: MockHostStorageMap(t, &MockStorageScan{"host1", noStorageScan}),
			},
		},
		"no storage": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: noStorageScan,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", noStorageScan}),
			},
		},
		"single host": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: standardScan,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", standardScan}),
			},
		},
		"single host with namespace": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: withNamespacesScan,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", withNamespacesScan}),
			},
		},
		"single host with space utilization": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: withSpaceUsageScan,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", withSpaceUsageScan}),
			},
		},
		"two hosts same scan": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: standardScan,
						},
						{
							Addr: "host2",
							// Use a newly-generated mock here to verify that
							// non-hashable fields are ignored.
							Message: MockServerScanResp(t, "standard"),
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1,host2", standardScan}),
			},
		},
		"two hosts different scans": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: noNVMEScan,
						},
						{
							Addr:    "host2",
							Message: noSCMScan,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage: MockHostStorageMap(t,
					&MockStorageScan{"host1", noNVMEScan},
					&MockStorageScan{"host2", noSCMScan},
				),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctx := context.TODO()
			mi := NewMockInvoker(log, tc.mic)

			gotResponse, gotErr := StorageScan(ctx, mi, &StorageScanReq{})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResponse, gotResponse, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_StorageFormat(t *testing.T) {
	for name, tc := range map[string]struct {
		mic         *MockInvokerConfig
		reformat    bool
		expResponse *StorageFormatResp
		expErr      error
	}{
		"empty response": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{},
			},
			expResponse: new(StorageFormatResp),
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
							Message: &ctlpb.StorageFormatResp{},
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
			expResponse: &StorageFormatResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "failed"}),
				HostStorage:    nil,
			},
		},
		"2 SCM, 2 NVMe; first SCM fails": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.StorageFormatResp{
								Mrets: []*ctlpb.ScmMountResult{
									{
										Mntpoint: "/mnt/1",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
											Error:  "/mnt/1 format failed",
										},
									},
									{
										Mntpoint: "/mnt/2",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
								},
								Crets: []*ctlpb.NvmeControllerResult{
									{
										State: &ctlpb.ResponseState{
											Info: "NVMe format skipped",
										},
									},
									{
										Pciaddr: "2",
									},
								},
							},
						},
					},
				},
			},
			expResponse: MockFormatResp(t, MockFormatConf{
				Hosts:       1,
				ScmPerHost:  2,
				ScmFailures: MockFailureMap(0),
				NvmePerHost: 2,
			}),
		},
		"2 SCM, 2 NVMe; second NVMe fails": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.StorageFormatResp{
								Mrets: []*ctlpb.ScmMountResult{
									{
										Mntpoint: "/mnt/1",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
									{
										Mntpoint: "/mnt/2",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
								},
								Crets: []*ctlpb.NvmeControllerResult{
									{
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
										Pciaddr: "1",
									},
									{
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
											Error:  "NVMe device 2 format failed",
										},
									},
								},
							},
						},
					},
				},
			},
			expResponse: MockFormatResp(t, MockFormatConf{
				Hosts:        1,
				ScmPerHost:   2,
				NvmePerHost:  2,
				NvmeFailures: MockFailureMap(1),
			}),
		},
		"2 SCM, 2 NVMe": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.StorageFormatResp{
								Mrets: []*ctlpb.ScmMountResult{
									{
										Mntpoint: "/mnt/1",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
									{
										Mntpoint: "/mnt/2",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
								},
								Crets: []*ctlpb.NvmeControllerResult{
									{
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
										Pciaddr: "1",
									},
									{
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
										Pciaddr: "2",
									},
								},
							},
						},
					},
				},
			},
			expResponse: MockFormatResp(t, MockFormatConf{
				Hosts:       1,
				ScmPerHost:  2,
				NvmePerHost: 2,
			}),
		},
		"2 Hosts, 2 SCM, 2 NVMe": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr: "host1",
							Message: &ctlpb.StorageFormatResp{
								Mrets: []*ctlpb.ScmMountResult{
									{
										Mntpoint: "/mnt/1",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
									{
										Mntpoint: "/mnt/2",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
								},
								Crets: []*ctlpb.NvmeControllerResult{
									{
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
										Pciaddr: "1",
									},
									{
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
										Pciaddr: "2",
									},
								},
							},
						},
						{
							Addr: "host2",
							Message: &ctlpb.StorageFormatResp{
								Mrets: []*ctlpb.ScmMountResult{
									{
										Mntpoint: "/mnt/1",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
									{
										Mntpoint: "/mnt/2",
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
									},
								},
								Crets: []*ctlpb.NvmeControllerResult{
									{
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
										Pciaddr: "1",
									},
									{
										State: &ctlpb.ResponseState{
											Status: ctlpb.ResponseStatus_CTL_SUCCESS,
										},
										Pciaddr: "2",
									},
								},
							},
						},
					},
				},
			},
			expResponse: MockFormatResp(t, MockFormatConf{
				Hosts:       2,
				ScmPerHost:  2,
				NvmePerHost: 2,
			}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctx := context.TODO()
			mi := NewMockInvoker(log, tc.mic)

			gotResponse, gotErr := StorageFormat(ctx, mi, &StorageFormatReq{Reformat: tc.reformat})
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResponse, gotResponse, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
