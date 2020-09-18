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
// +build firmware

package server

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

func getPBNvmeQueryResults(t *testing.T, devs storage.NvmeControllers) []*ctlpb.NvmeFirmwareQueryResp {
	results := make([]*ctlpb.NvmeFirmwareQueryResp, 0, len(devs))
	for _, dev := range devs {
		devPB := &ctlpb.NvmeFirmwareQueryResp{}
		if err := convert.Types(dev, &devPB.Device); err != nil {
			t.Fatalf("unable to convert NvmeController: %s", err)
		}
		results = append(results, devPB)
	}

	return results
}

func TestCtlSvc_FirmwareQuery(t *testing.T) {
	testFWInfo := &storage.ScmFirmwareInfo{
		ActiveVersion:     "MyActiveVersion",
		StagedVersion:     "MyStagedVersion",
		ImageMaxSizeBytes: 1024,
		UpdateStatus:      storage.ScmUpdateStatusStaged,
	}

	testNVMeDevs := storage.MockNvmeControllers(3)
	testNVMePB := getPBNvmeQueryResults(t, testNVMeDevs)

	for name, tc := range map[string]struct {
		bmbc    *bdev.MockBackendConfig
		smbc    *scm.MockBackendConfig
		req     ctlpb.FirmwareQueryReq
		expErr  error
		expResp *ctlpb.FirmwareQueryResp
	}{
		"nothing requested": {
			expResp: &ctlpb.FirmwareQueryResp{},
		},
		"SCM - discovery failed": {
			req: ctlpb.FirmwareQueryReq{
				QueryScm: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("mock discovery failed"),
			},
			expErr: errors.New("mock discovery failed"),
		},
		"SCM - no devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryScm: true,
			},
			smbc: &scm.MockBackendConfig{},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{},
			},
		},
		"SCM - success with devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryScm: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{
					{UID: "TestUid1"},
					{UID: "TestUid2"},
					{UID: "TestUid3"},
				},
				GetFirmwareStatusRes: testFWInfo,
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid1"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid2"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid3"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
			},
		},
		"SCM - errors with devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryScm: true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{
					{UID: "TestUid1"},
					{UID: "TestUid2"},
				},
				GetFirmwareStatusErr: errors.New("mock query"),
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module: &ctlpb.ScmModule{Uid: "TestUid1"},
						Error:  "mock query",
					},
					{
						Module: &ctlpb.ScmModule{Uid: "TestUid2"},
						Error:  "mock query",
					},
				},
			},
		},
		"NVMe - discovery failed": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("mock scan failed"),
			},
			expErr: errors.New("mock scan failed"),
		},
		"NVMe - no devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
			},
			bmbc: &bdev.MockBackendConfig{},
			expResp: &ctlpb.FirmwareQueryResp{
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{},
			},
		},
		"NVMe - success with devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				NvmeResults: testNVMePB,
			},
		},
		"both - success with devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				QueryScm:  true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{
					{UID: "TestUid1"},
					{UID: "TestUid2"},
					{UID: "TestUid3"},
				},
				GetFirmwareStatusRes: testFWInfo,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid1"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid2"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid3"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
				NvmeResults: testNVMePB,
			},
		},
		"both - no SCM found": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				QueryScm:  true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults:  []*ctlpb.ScmFirmwareQueryResp{},
				NvmeResults: testNVMePB,
			},
		},
		"both - no NVMe found": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				QueryScm:  true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{
					{UID: "TestUid1"},
					{UID: "TestUid2"},
					{UID: "TestUid3"},
				},
				GetFirmwareStatusRes: testFWInfo,
			},
			bmbc: &bdev.MockBackendConfig{},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid1"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid2"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            &ctlpb.ScmModule{Uid: "TestUid3"},
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			config := emptyMockConfig(t)
			cs := mockControlService(t, log, config, tc.bmbc, tc.smbc, nil)

			resp, err := cs.FirmwareQuery(context.TODO(), &tc.req)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestCtlSvc_FirmwareUpdate(t *testing.T) {
	mockNVMe := storage.MockNvmeControllers(3)

	for name, tc := range map[string]struct {
		bmbc           *bdev.MockBackendConfig
		smbc           *scm.MockBackendConfig
		serversRunning bool
		noRankServers  bool
		req            ctlpb.FirmwareUpdateReq
		expErr         error
		expResp        *ctlpb.FirmwareUpdateResp
	}{
		"IO servers running": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
			},
			serversRunning: true,
			expErr:         FaultInstancesNotStopped("firmware update", 0),
		},
		"IO servers running with no rank": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
			},
			serversRunning: true,
			noRankServers:  true,
			expErr:         errors.New("unidentified server rank is running"),
		},
		"no path": {
			req: ctlpb.FirmwareUpdateReq{
				Type: ctlpb.FirmwareUpdateReq_SCM,
			},
			expErr: errors.New("missing path to firmware file"),
		},
		"invalid device type": {
			req: ctlpb.FirmwareUpdateReq{
				Type: ctlpb.FirmwareUpdateReq_DeviceType(0xFFFF),
			},
			expErr: errors.New("unrecognized device type"),
		},
		"SCM - discovery failed": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("mock discovery failed"),
			},
			expErr: errors.New("mock discovery failed"),
		},
		"SCM - no devices": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
			},
			smbc:   &scm.MockBackendConfig{},
			expErr: errors.New("no SCM modules"),
		},
		"SCM - success with devices": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{
					{UID: "TestUid1"},
					{UID: "TestUid2"},
					{UID: "TestUid3"},
				},
				UpdateFirmwareErr: nil,
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				ScmResults: []*ctlpb.ScmFirmwareUpdateResp{
					{
						Module: &ctlpb.ScmModule{Uid: "TestUid1"},
					},
					{
						Module: &ctlpb.ScmModule{Uid: "TestUid2"},
					},
					{
						Module: &ctlpb.ScmModule{Uid: "TestUid3"},
					},
				},
			},
		},
		"SCM - failed with devices": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes: storage.ScmModules{
					{UID: "TestUid1"},
					{UID: "TestUid2"},
					{UID: "TestUid3"},
				},
				UpdateFirmwareErr: errors.New("mock update"),
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				ScmResults: []*ctlpb.ScmFirmwareUpdateResp{
					{
						Module: &ctlpb.ScmModule{Uid: "TestUid1"},
						Error:  "mock update",
					},
					{
						Module: &ctlpb.ScmModule{Uid: "TestUid2"},
						Error:  "mock update",
					},
					{
						Module: &ctlpb.ScmModule{Uid: "TestUid3"},
						Error:  "mock update",
					},
				},
			},
		},
		"NVMe - scan failed": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_NVMe,
				FirmwarePath: "/some/path",
			},
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("mock scan failed"),
			},
			expErr: errors.New("mock scan failed"),
		},
		"NVMe - no devices": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_NVMe,
				FirmwarePath: "/some/path",
			},
			bmbc:   &bdev.MockBackendConfig{},
			expErr: errors.New("no NVMe device controllers"),
		},
		"NVMe - success with devices": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_NVMe,
				FirmwarePath: "/some/path",
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: mockNVMe},
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				NvmeResults: []*ctlpb.NvmeFirmwareUpdateResp{
					{
						PciAddr: mockNVMe[0].PciAddr,
					},
					{
						PciAddr: mockNVMe[1].PciAddr,
					},
					{
						PciAddr: mockNVMe[2].PciAddr,
					},
				},
			},
		},
		"NVMe - failure with devices": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_NVMe,
				FirmwarePath: "/some/path",
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes:   &bdev.ScanResponse{Controllers: mockNVMe},
				UpdateErr: errors.New("mock update"),
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				NvmeResults: []*ctlpb.NvmeFirmwareUpdateResp{
					{
						PciAddr: mockNVMe[0].PciAddr,
						Error:   "mock update",
					},
					{
						PciAddr: mockNVMe[1].PciAddr,
						Error:   "mock update",
					},
					{
						PciAddr: mockNVMe[2].PciAddr,
						Error:   "mock update",
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := emptyMockConfig(t)
			cs := mockControlService(t, log, cfg, tc.bmbc, tc.smbc, nil)
			for i := 0; i < 2; i++ {
				runner := ioserver.NewTestRunner(&ioserver.TestRunnerConfig{
					Running: atm.NewBool(tc.serversRunning),
				}, ioserver.NewConfig())
				instance := NewIOServerInstance(log, nil, nil, nil, runner)
				if !tc.noRankServers {
					instance._superblock = &Superblock{}
					instance._superblock.ValidRank = true
					instance._superblock.Rank = system.NewRankPtr(uint32(i))
				}
				if err := cs.harness.AddInstance(instance); err != nil {
					t.Fatal(err)
				}
			}

			resp, err := cs.FirmwareUpdate(context.TODO(), &tc.req)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
