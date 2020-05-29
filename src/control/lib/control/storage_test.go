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
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type storageScanVariant int

const (
	standard storageScanVariant = iota
	withNamespace
	noNVME
	noSCM
	noStorage
	scmFailed
	nvmeFailed
	bothFailed
)

func standardServerScanResponse(t *testing.T) *ctlpb.StorageScanResp {
	pbSsr := &ctlpb.StorageScanResp{
		Nvme: &ctlpb.ScanNvmeResp{},
		Scm:  &ctlpb.ScanScmResp{},
	}
	nvmeControllers := storage.NvmeControllers{
		storage.MockNvmeController(),
	}
	scmModules := storage.ScmModules{
		storage.MockScmModule(),
	}
	if err := convert.Types(nvmeControllers, &pbSsr.Nvme.Ctrlrs); err != nil {
		t.Fatal(err)
	}
	if err := convert.Types(scmModules, &pbSsr.Scm.Modules); err != nil {
		t.Fatal(err)
	}

	return pbSsr
}

func mockServerScanResponse(t *testing.T, variant storageScanVariant) *ctlpb.StorageScanResp {
	ssr := standardServerScanResponse(t)
	switch variant {
	case withNamespace:
		scmNamespaces := storage.ScmNamespaces{
			storage.MockScmNamespace(),
		}
		if err := convert.Types(scmNamespaces, &ssr.Scm.Namespaces); err != nil {
			t.Fatal(err)
		}
	case noNVME:
		ssr.Nvme.Ctrlrs = nil
	case noSCM:
		ssr.Scm.Modules = nil
	case noStorage:
		ssr.Nvme.Ctrlrs = nil
		ssr.Scm.Modules = nil
	case scmFailed:
		ssr.Scm.Modules = nil
		ssr.Scm.State = &ctlpb.ResponseState{
			Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
			Error:  "scm scan failed",
		}
	case nvmeFailed:
		ssr.Nvme.Ctrlrs = nil
		ssr.Nvme.State = &ctlpb.ResponseState{
			Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
			Error:  "nvme scan failed",
		}
	case bothFailed:
		ssr.Scm.Modules = nil
		ssr.Scm.State = &ctlpb.ResponseState{
			Status: ctlpb.ResponseStatus_CTL_ERR_SCM,
			Error:  "scm scan failed",
		}
		ssr.Nvme.Ctrlrs = nil
		ssr.Nvme.State = &ctlpb.ResponseState{
			Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
			Error:  "nvme scan failed",
		}
	}
	return ssr
}

func mockHostStorageSet(t *testing.T, hosts string, pbResp *ctlpb.StorageScanResp) *HostStorageSet {
	hss := &HostStorageSet{
		HostStorage: &HostStorage{},
		HostSet:     mockHostSet(t, hosts),
	}

	if err := convert.Types(pbResp.GetNvme().GetCtrlrs(), &hss.HostStorage.NvmeDevices); err != nil {
		t.Fatal(err)
	}
	if err := convert.Types(pbResp.GetScm().GetModules(), &hss.HostStorage.ScmModules); err != nil {
		t.Fatal(err)
	}
	if err := convert.Types(pbResp.GetScm().GetNamespaces(), &hss.HostStorage.ScmNamespaces); err != nil {
		t.Fatal(err)
	}

	return hss
}

type mockStorageScan struct {
	Hosts    string
	HostScan *ctlpb.StorageScanResp
}

func mockHostStorageMap(t *testing.T, scans ...*mockStorageScan) HostStorageMap {
	hsm := make(HostStorageMap)

	for _, scan := range scans {
		hss := mockHostStorageSet(t, scan.Hosts, scan.HostScan)
		hk, err := hss.HostStorage.HashKey()
		if err != nil {
			t.Fatal(err)
		}
		hsm[hk] = hss
	}

	return hsm
}

func TestControl_StorageScan(t *testing.T) {
	var (
		standardScan       = mockServerScanResponse(t, standard)
		withNamespacesScan = mockServerScanResponse(t, withNamespace)
		noNVMEScan         = mockServerScanResponse(t, noNVME)
		noSCMScan          = mockServerScanResponse(t, noSCM)
		noStorageScan      = mockServerScanResponse(t, noStorage)
		scmScanFailed      = mockServerScanResponse(t, scmFailed)
		nvmeScanFailed     = mockServerScanResponse(t, nvmeFailed)
		bothScansFailed    = mockServerScanResponse(t, bothFailed)
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host1", "failed"}),
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host1", "scm scan failed"}),
				HostStorage:    mockHostStorageMap(t, &mockStorageScan{"host1", scmScanFailed}),
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host1", "nvme scan failed"}),
				HostStorage:    mockHostStorageMap(t, &mockStorageScan{"host1", noNVMEScan}),
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
				HostErrorsResp: mockHostErrorsResp(t,
					&mockHostError{"host1", "nvme scan failed"},
					&mockHostError{"host1", "scm scan failed"},
				),
				HostStorage: mockHostStorageMap(t, &mockStorageScan{"host1", noStorageScan}),
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
				HostErrorsResp: mockHostErrorsResp(t),
				HostStorage:    mockHostStorageMap(t, &mockStorageScan{"host1", noStorageScan}),
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
				HostErrorsResp: mockHostErrorsResp(t),
				HostStorage:    mockHostStorageMap(t, &mockStorageScan{"host1", standardScan}),
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
				HostErrorsResp: mockHostErrorsResp(t),
				HostStorage:    mockHostStorageMap(t, &mockStorageScan{"host1", withNamespacesScan}),
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
							Message: mockServerScanResponse(t, standard),
						},
					},
				},
			},
			expResponse: &StorageScanResp{
				HostErrorsResp: mockHostErrorsResp(t),
				HostStorage:    mockHostStorageMap(t, &mockStorageScan{"host1,host2", standardScan}),
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
				HostErrorsResp: mockHostErrorsResp(t),
				HostStorage: mockHostStorageMap(t,
					&mockStorageScan{"host1", noNVMEScan},
					&mockStorageScan{"host2", noSCMScan},
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

func mockFailureMap(idxList ...int) map[int]struct{} {
	fm := make(map[int]struct{})
	for _, i := range idxList {
		fm[i] = struct{}{}
	}
	return fm
}

type mockFormatConf struct {
	hosts        int
	scmPerHost   int
	nvmePerHost  int
	scmFailures  map[int]struct{}
	nvmeFailures map[int]struct{}
}

func mockFormatResp(t *testing.T, mfc mockFormatConf) *StorageFormatResp {
	hem := make(HostErrorsMap)
	hsm := make(HostStorageMap)

	for i := 0; i < mfc.hosts; i++ {
		hs := &HostStorage{}
		hostName := fmt.Sprintf("host%d", i+1)

		for j := 0; j < mfc.scmPerHost; j++ {
			if _, failed := mfc.scmFailures[j]; failed {
				if err := hem.Add(hostName, errors.Errorf("/mnt/%d format failed", j+1)); err != nil {
					t.Fatal(err)
				}
				continue
			}
			hs.ScmMountPoints = append(hs.ScmMountPoints, &storage.ScmMountPoint{
				Info: ctlpb.ResponseStatus_CTL_SUCCESS.String(),
				Path: fmt.Sprintf("/mnt/%d", j+1),
			})
		}

		for j := 0; j < mfc.nvmePerHost; j++ {
			if _, failed := mfc.nvmeFailures[j]; failed {
				if err := hem.Add(hostName, errors.Errorf("NVMe device %d format failed", j+1)); err != nil {
					t.Fatal(err)
				}
				continue
			}

			// If the SCM format/mount failed for this idx, then there shouldn't
			// be an NVMe format result.
			if _, failed := mfc.scmFailures[j]; failed {
				continue
			}
			hs.NvmeDevices = append(hs.NvmeDevices, &storage.NvmeController{
				Info:    ctlpb.ResponseStatus_CTL_SUCCESS.String(),
				PciAddr: fmt.Sprintf("%d", j+1),
			})
		}
		if err := hsm.Add(hostName, hs); err != nil {
			t.Fatal(err)
		}
	}

	if len(hem) == 0 {
		hem = nil
	}
	return &StorageFormatResp{
		HostErrorsResp: HostErrorsResp{
			HostErrors: hem,
		},
		HostStorage: hsm,
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
				HostErrorsResp: mockHostErrorsResp(t, &mockHostError{"host1", "failed"}),
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
			expResponse: mockFormatResp(t, mockFormatConf{
				hosts:       1,
				scmPerHost:  2,
				scmFailures: mockFailureMap(0),
				nvmePerHost: 2,
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
			expResponse: mockFormatResp(t, mockFormatConf{
				hosts:        1,
				scmPerHost:   2,
				nvmePerHost:  2,
				nvmeFailures: mockFailureMap(1),
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
			expResponse: mockFormatResp(t, mockFormatConf{
				hosts:       1,
				scmPerHost:  2,
				nvmePerHost: 2,
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
			expResponse: mockFormatResp(t, mockFormatConf{
				hosts:       2,
				scmPerHost:  2,
				nvmePerHost: 2,
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
