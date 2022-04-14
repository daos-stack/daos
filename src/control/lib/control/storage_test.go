//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func TestControl_StorageMap(t *testing.T) {
	for name, tc := range map[string]struct {
		hss       []*HostStorage
		expHsmLen int
		expErr    error
	}{
		"matching zero values": {
			hss: []*HostStorage{
				{},
				{},
			},
			expHsmLen: 1,
		},
		"matching empty values": {
			hss: []*HostStorage{
				{
					NvmeDevices:   storage.NvmeControllers{},
					ScmNamespaces: storage.ScmNamespaces{},
				},
				{
					NvmeDevices:   storage.NvmeControllers{},
					ScmNamespaces: storage.ScmNamespaces{},
				},
			},
			expHsmLen: 1,
		},
		"mismatch non-empty nvme devices": {
			hss: []*HostStorage{
				{
					NvmeDevices:   storage.NvmeControllers{},
					ScmNamespaces: storage.ScmNamespaces{},
				},
				{
					NvmeDevices:   storage.NvmeControllers{},
					ScmNamespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
				},
			},
			expHsmLen: 2,
		},
		// NOTE: current implementation does not distinguish between nil
		//       and empty slices when hashing during HostStorageMap.Add
		// "mismatch nil and empty nvme devices": {
		// 	hss: []*HostStorage{
		// 		{
		// 			NvmeDevices:   storage.NvmeControllers{},
		// 			ScmNamespaces: storage.ScmNamespaces{},
		// 		},
		// 		{
		// 			NvmeDevices:   nil,
		// 			ScmNamespaces: storage.ScmNamespaces{},
		// 		},
		// 	},
		// 	expHsmLen: 2,
		// },
		"mismatch reboot required": {
			hss: []*HostStorage{
				{
					NvmeDevices:    storage.NvmeControllers{},
					ScmNamespaces:  storage.ScmNamespaces{storage.MockScmNamespace(0)},
					RebootRequired: false,
				},
				{
					NvmeDevices:    storage.NvmeControllers{},
					ScmNamespaces:  storage.ScmNamespaces{storage.MockScmNamespace(0)},
					RebootRequired: true,
				},
			},
			expHsmLen: 2,
		},
		"mismatch nvme capacity": {
			hss: []*HostStorage{
				{
					NvmeDevices: storage.NvmeControllers{
						&storage.NvmeController{
							Namespaces: []*storage.NvmeNamespace{
								{
									Size: uint64(humanize.TByte),
								},
							},
						},
					},
					ScmNamespaces: storage.ScmNamespaces{storage.MockScmNamespace(0)},
				},
				{
					NvmeDevices: storage.NvmeControllers{
						&storage.NvmeController{
							Namespaces: []*storage.NvmeNamespace{
								{
									Size: uint64(humanize.TByte * 2),
								},
							},
						},
					},
					ScmNamespaces: storage.ScmNamespaces{storage.MockScmNamespace(0)},
				},
			},
			expHsmLen: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			hsm := make(HostStorageMap)

			for i, hs := range tc.hss {
				gotErr := hsm.Add(fmt.Sprintf("h%d", i), hs)
				common.CmpErr(t, tc.expErr, gotErr)
				if tc.expErr != nil {
					return
				}
			}

			common.AssertEqual(t, tc.expHsmLen, len(hsm), "unexpected number of keys in map")
		})
	}
}

func TestControl_StorageScan(t *testing.T) {
	var (
		standard       = MockServerScanResp(t, "standard")
		pmemA          = MockServerScanResp(t, "pmemA")
		withSpaceUsage = MockServerScanResp(t, "withSpaceUsage")
		noNvme         = MockServerScanResp(t, "noNvme")
		noScm          = MockServerScanResp(t, "noScm")
		noStorage      = MockServerScanResp(t, "noStorage")
		scmFailed      = MockServerScanResp(t, "scmFailed")
		nvmeFailed     = MockServerScanResp(t, "nvmeFailed")
		bothFailed     = MockServerScanResp(t, "bothFailed")
		nvmeBasicA     = MockServerScanResp(t, "nvmeBasicA")
		nvmeBasicB     = MockServerScanResp(t, "nvmeBasicB")
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
							Message: standard,
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
							Message: scmFailed,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "scm scan failed"}),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", scmFailed}),
			},
		},
		"nvme scan error": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: nvmeFailed,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "nvme scan failed"}),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", noNvme}),
			},
		},
		"scm and nvme scan error": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: bothFailed,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t,
					&MockHostError{"host1", "nvme scan failed"},
					&MockHostError{"host1", "scm scan failed"},
				),
				HostStorage: MockHostStorageMap(t, &MockStorageScan{"host1", noStorage}),
			},
		},
		"no storage": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: noStorage,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", noStorage}),
			},
		},
		"single host": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: standard,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", standard}),
			},
		},
		"single host with namespaces": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: pmemA,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", pmemA}),
			},
		},
		"single host with space utilization": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: withSpaceUsage,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1", withSpaceUsage}),
			},
		},
		"two hosts same scan": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: standard,
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
				HostStorage:    MockHostStorageMap(t, &MockStorageScan{"host1,host2", standard}),
			},
		},
		"two hosts different scans": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: noNvme,
						},
						{
							Addr:    "host2",
							Message: noScm,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage: MockHostStorageMap(t,
					&MockStorageScan{"host1", noNvme},
					&MockStorageScan{"host2", noScm},
				),
			},
		},
		"two hosts different nvme capacity": {
			mic: &MockInvokerConfig{
				UnaryResponse: &UnaryResponse{
					Responses: []*HostResponse{
						{
							Addr:    "host1",
							Message: nvmeBasicA,
						},
						{
							Addr:    "host2",
							Message: nvmeBasicB,
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: MockHostErrorsResp(t),
				HostStorage: MockHostStorageMap(t,
					&MockStorageScan{"host1", nvmeBasicA},
					&MockStorageScan{"host2", nvmeBasicB},
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
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", errMSConnectionFailure, nil),
					{},
				},
			},
			expResponse: new(StorageFormatResp),
		},
		"nil message": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", errMSConnectionFailure, nil),
					{
						Responses: []*HostResponse{
							{
								Addr: "host1",
							},
						},
					},
				},
			},
			expErr: errors.New("unpack"),
		},
		"bad host addr": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", system.ErrRaftUnavail, nil),
					{
						Responses: []*HostResponse{
							{
								Addr:    ",",
								Message: &ctlpb.StorageFormatResp{},
							},
						},
					},
				},
			},
			expErr: errors.New("invalid hostname"),
		},
		"bad host addr with error": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", system.ErrRaftUnavail, nil),
					{
						Responses: []*HostResponse{
							{
								Addr:  ",",
								Error: errors.New("banana"),
							},
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
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", system.ErrRaftUnavail, nil),
					{
						Responses: []*HostResponse{
							{
								Addr:  "host1",
								Error: errors.New("failed"),
							},
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
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", system.ErrRaftUnavail, nil),
					{
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
											State:    &ctlpb.ResponseState{},
										},
									},
									Crets: []*ctlpb.NvmeControllerResult{
										{
											State: &ctlpb.ResponseState{
												Info: "NVMe format skipped",
											},
										},
										{
											PciAddr: "2",
											State:   &ctlpb.ResponseState{},
										},
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
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", system.ErrRaftUnavail, nil),
					{
						Responses: []*HostResponse{
							{
								Addr: "host1",
								Message: &ctlpb.StorageFormatResp{
									Mrets: []*ctlpb.ScmMountResult{
										{
											Mntpoint: "/mnt/1",
											State:    &ctlpb.ResponseState{},
										},
										{
											Mntpoint: "/mnt/2",
											State:    &ctlpb.ResponseState{},
										},
									},
									Crets: []*ctlpb.NvmeControllerResult{
										{
											State:   &ctlpb.ResponseState{},
											PciAddr: "1",
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
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", system.ErrRaftUnavail, nil),
					{
						Responses: []*HostResponse{
							{
								Addr: "host1",
								Message: &ctlpb.StorageFormatResp{
									Mrets: []*ctlpb.ScmMountResult{
										{
											Mntpoint: "/mnt/1",
											State:    &ctlpb.ResponseState{},
										},
										{
											Mntpoint: "/mnt/2",
											State:    &ctlpb.ResponseState{},
										},
									},
									Crets: []*ctlpb.NvmeControllerResult{
										{
											State:   &ctlpb.ResponseState{},
											PciAddr: "1",
										},
										{
											State:   &ctlpb.ResponseState{},
											PciAddr: "2",
										},
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
				UnaryResponseSet: []*UnaryResponse{
					MockMSResponse("", system.ErrRaftUnavail, nil),
					{
						Responses: []*HostResponse{
							{
								Addr: "host1",
								Message: &ctlpb.StorageFormatResp{
									Mrets: []*ctlpb.ScmMountResult{
										{
											Mntpoint: "/mnt/1",
											State:    &ctlpb.ResponseState{},
										},
										{
											Mntpoint: "/mnt/2",
											State:    &ctlpb.ResponseState{},
										},
									},
									Crets: []*ctlpb.NvmeControllerResult{
										{
											State:   &ctlpb.ResponseState{},
											PciAddr: "1",
										},
										{
											State:   &ctlpb.ResponseState{},
											PciAddr: "2",
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
											State:    &ctlpb.ResponseState{},
										},
										{
											Mntpoint: "/mnt/2",
											State:    &ctlpb.ResponseState{},
										},
									},
									Crets: []*ctlpb.NvmeControllerResult{
										{
											State:   &ctlpb.ResponseState{},
											PciAddr: "1",
										},
										{
											State:   &ctlpb.ResponseState{},
											PciAddr: "2",
										},
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

func TestControl_checkFormatReq(t *testing.T) {
	reqHosts := func(h ...string) []string {
		return h
	}
	localServer := DefaultConfig().HostList[0]

	for name, tc := range map[string]struct {
		reqHosts   []string
		invokerErr error
		responses  []*UnaryResponse
		expErr     error
	}{
		"localserver not running": {
			responses: []*UnaryResponse{
				MockMSResponse(localServer, system.ErrRaftUnavail, nil),
			},
		},
		"localserver running": {
			responses: []*UnaryResponse{
				MockMSResponse(localServer, nil, &mgmtpb.SystemQueryResp{}),
			},
			expErr: FaultFormatRunningSystem,
		},
		"non-replica no MS running": {
			reqHosts: reqHosts("non-replica"),
			responses: []*UnaryResponse{
				MockMSResponse("non-replica", &system.ErrNotReplica{Replicas: []string{"replica"}}, nil),
				MockMSResponse("replica", errMSConnectionFailure, nil),
			},
		},
		"replica not running": {
			reqHosts: reqHosts("replica"),
			responses: []*UnaryResponse{
				MockMSResponse("replica", system.ErrRaftUnavail, nil),
			},
		},
		"replica running": {
			reqHosts: reqHosts("replica"),
			responses: []*UnaryResponse{
				MockMSResponse("replica", nil, &mgmtpb.SystemQueryResp{}),
			},
			expErr: FaultFormatRunningSystem,
		},
		"system unformatted": {
			reqHosts: reqHosts("replica"),
			responses: []*UnaryResponse{
				MockMSResponse("replica", system.ErrUninitialized, nil),
			},
		},
		"system query fails": {
			reqHosts: reqHosts("replica"),
			responses: []*UnaryResponse{
				MockMSResponse("replica", errors.New("oops"), nil),
			},
			expErr: errors.New("oops"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:       tc.invokerErr,
				UnaryResponseSet: tc.responses,
			})

			req := &StorageFormatReq{}
			req.SetHostList(tc.reqHosts)
			err := checkFormatReq(context.Background(), mi, req)
			common.CmpErr(t, tc.expErr, err)

		})
	}
}

func TestControl_StorageNvmeRebind(t *testing.T) {
	for name, tc := range map[string]struct {
		mic         *MockInvokerConfig
		pciAddr     string
		expResponse *NvmeRebindResp
		expErr      error
	}{
		"empty pci address": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{},
				},
			},
			expErr: errors.New("invalid pci address"),
		},
		"invalid pci address": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{},
				},
			},
			pciAddr: "ZZZZ:MM:NN.O",
			expErr:  errors.New("invalid pci address"),
		},
		"empty response": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{},
				},
			},
			pciAddr:     common.MockPCIAddr(),
			expResponse: new(NvmeRebindResp),
		},
		"invoke fails": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("failed"),
			},
			pciAddr: common.MockPCIAddr(),
			expErr:  errors.New("failed"),
		},
		"server error": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{
						Responses: []*HostResponse{
							{
								Addr:  "host1",
								Error: errors.New("failed"),
							},
						},
					},
				},
			},
			pciAddr: common.MockPCIAddr(),
			expResponse: &NvmeRebindResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "failed"}),
			},
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{
						Responses: []*HostResponse{
							{
								Addr:    "host1",
								Message: &ctlpb.NvmeRebindResp{},
							},
						},
					},
				},
			},
			pciAddr:     common.MockPCIAddr(),
			expResponse: &NvmeRebindResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctx := context.TODO()
			mi := NewMockInvoker(log, tc.mic)

			gotResponse, gotErr := StorageNvmeRebind(ctx, mi, &NvmeRebindReq{
				PCIAddr: tc.pciAddr,
			})
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

func TestControl_StorageNvmeAddDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		mic         *MockInvokerConfig
		pciAddr     string
		expResponse *NvmeAddDeviceResp
		expErr      error
	}{
		"empty pci address": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{},
				},
			},
			expErr: errors.New("invalid pci address"),
		},
		"invalid pci address": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{},
				},
			},
			pciAddr: "ZZZZ:MM:NN.O",
			expErr:  errors.New("invalid pci address"),
		},
		"empty response": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{},
				},
			},
			pciAddr:     common.MockPCIAddr(),
			expResponse: new(NvmeAddDeviceResp),
		},
		"invoke fails": {
			mic: &MockInvokerConfig{
				UnaryError: errors.New("failed"),
			},
			pciAddr: common.MockPCIAddr(),
			expErr:  errors.New("failed"),
		},
		"server error": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{
						Responses: []*HostResponse{
							{
								Addr:  "host1",
								Error: errors.New("failed"),
							},
						},
					},
				},
			},
			pciAddr: common.MockPCIAddr(),
			expResponse: &NvmeAddDeviceResp{
				HostErrorsResp: MockHostErrorsResp(t, &MockHostError{"host1", "failed"}),
			},
		},
		"success": {
			mic: &MockInvokerConfig{
				UnaryResponseSet: []*UnaryResponse{
					{
						Responses: []*HostResponse{
							{
								Addr:    "host1",
								Message: &ctlpb.NvmeAddDeviceResp{},
							},
						},
					},
				},
			},
			pciAddr:     common.MockPCIAddr(),
			expResponse: &NvmeAddDeviceResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			ctx := context.TODO()
			mi := NewMockInvoker(log, tc.mic)

			gotResponse, gotErr := StorageNvmeAddDevice(ctx, mi, &NvmeAddDeviceReq{
				PCIAddr:          tc.pciAddr,
				EngineIndex:      0,
				StorageTierIndex: -1,
			})
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
