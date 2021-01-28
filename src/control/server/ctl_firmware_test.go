//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
	"github.com/daos-stack/daos/src/control/server/config"
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

func getProtoNVMeControllers(t *testing.T, ctrlrs storage.NvmeControllers) []*ctlpb.NvmeController {
	results := make([]*ctlpb.NvmeController, 0, len(ctrlrs))
	for _, c := range ctrlrs {
		cPB := &ctlpb.NvmeController{}
		if err := convert.Types(c, &cPB); err != nil {
			t.Fatalf("unable to convert NvmeController: %s", err)
		}
		results = append(results, cPB)
	}

	return results
}

func getProtoScmModules(t *testing.T, modules storage.ScmModules) []*ctlpb.ScmModule {
	results := make([]*ctlpb.ScmModule, 0, len(modules))
	for _, mod := range modules {
		modPB := &ctlpb.ScmModule{}
		if err := convert.Types(mod, &modPB); err != nil {
			t.Fatalf("unable to convert ScmModule: %s", err)
		}
		results = append(results, modPB)
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

	mockSCM := storage.MockScmModules(3)
	mockPbSCM := getProtoScmModules(t, mockSCM)

	testNVMeDevs := storage.MockNvmeControllers(3)
	mockPbNVMeDevs := getProtoNVMeControllers(t, testNVMeDevs)
	testNVMeResults := getPBNvmeQueryResults(t, testNVMeDevs)

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
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            mockPbSCM[0],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            mockPbSCM[1],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            mockPbSCM[2],
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
				DiscoverRes:          mockSCM,
				GetFirmwareStatusErr: errors.New("mock query"),
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module: mockPbSCM[0],
						Error:  "mock query",
					},
					{
						Module: mockPbSCM[1],
						Error:  "mock query",
					},
					{
						Module: mockPbSCM[2],
						Error:  "mock query",
					},
				},
			},
		},
		"SCM - filter by FW rev": {
			req: ctlpb.FirmwareQueryReq{
				QueryScm:    true,
				FirmwareRev: "FwRev0",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            mockPbSCM[0],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
			},
		},
		"SCM - filter by model ID": {
			req: ctlpb.FirmwareQueryReq{
				QueryScm: true,
				ModelID:  "PartNumber1",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            mockPbSCM[1],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
			},
		},
		"SCM - specific devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryScm:  true,
				DeviceIDs: []string{"Device1", "Device2"},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            mockPbSCM[1],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            mockPbSCM[2],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
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
				NvmeResults: testNVMeResults,
			},
		},
		"NVMe - filter by FW rev": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme:   true,
				FirmwareRev: "fwRev-0",
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{
					{
						Device: mockPbNVMeDevs[0],
					},
				},
			},
		},
		"NVMe - filter by model ID": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				ModelID:   "model-1",
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{
					{
						Device: mockPbNVMeDevs[1],
					},
				},
			},
		},
		"NVMe - specific devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				DeviceIDs: []string{"0000:80:00.1", "0000:80:00.2"},
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{
					{
						Device: mockPbNVMeDevs[1],
					},
					{
						Device: mockPbNVMeDevs[2],
					},
				},
			},
		},
		"both - success with devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				QueryScm:  true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            mockPbSCM[0],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            mockPbSCM[1],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            mockPbSCM[2],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
				NvmeResults: testNVMeResults,
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
				NvmeResults: testNVMeResults,
			},
		},
		"both - no NVMe found": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				QueryScm:  true,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			bmbc: &bdev.MockBackendConfig{},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            mockPbSCM[0],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            mockPbSCM[1],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
					{
						Module:            mockPbSCM[2],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{},
			},
		},
		"both - filter only catches SCM": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme:   true,
				QueryScm:    true,
				FirmwareRev: "FWRev0",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            mockPbSCM[0],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{},
			},
		},
		"both - filter only catches NVMe": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				QueryScm:  true,
				ModelID:   "model-0",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{},
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{
					{
						Device: mockPbNVMeDevs[0],
					},
				},
			},
		},
		"both - specific devices": {
			req: ctlpb.FirmwareQueryReq{
				QueryNvme: true,
				QueryScm:  true,
				DeviceIDs: []string{"0000:80:00.1", "Device0", "0000:80:00.2"},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:          mockSCM,
				GetFirmwareStatusRes: testFWInfo,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: testNVMeDevs},
			},
			expResp: &ctlpb.FirmwareQueryResp{
				ScmResults: []*ctlpb.ScmFirmwareQueryResp{
					{
						Module:            mockPbSCM[0],
						ActiveVersion:     testFWInfo.ActiveVersion,
						StagedVersion:     testFWInfo.StagedVersion,
						ImageMaxSizeBytes: testFWInfo.ImageMaxSizeBytes,
						UpdateStatus:      uint32(testFWInfo.UpdateStatus),
					},
				},
				NvmeResults: []*ctlpb.NvmeFirmwareQueryResp{
					{
						Device: mockPbNVMeDevs[1],
					},

					{
						Device: mockPbNVMeDevs[2],
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			config := config.DefaultServer()
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
	mockSCM := storage.MockScmModules(3)
	mockPbSCM := getProtoScmModules(t, mockSCM)
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
				DiscoverRes:       mockSCM,
				UpdateFirmwareErr: nil,
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				ScmResults: []*ctlpb.ScmFirmwareUpdateResp{
					{
						Module: mockPbSCM[0],
					},
					{
						Module: mockPbSCM[1],
					},
					{
						Module: mockPbSCM[2],
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
				DiscoverRes:       mockSCM,
				UpdateFirmwareErr: errors.New("mock update"),
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				ScmResults: []*ctlpb.ScmFirmwareUpdateResp{
					{
						Module: mockPbSCM[0],
						Error:  "mock update",
					},
					{
						Module: mockPbSCM[1],
						Error:  "mock update",
					},
					{
						Module: mockPbSCM[2],
						Error:  "mock update",
					},
				},
			},
		},
		"SCM - filter by FW rev": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
				FirmwareRev:  "FWRev2",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:       mockSCM,
				UpdateFirmwareErr: nil,
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				ScmResults: []*ctlpb.ScmFirmwareUpdateResp{
					{
						Module: mockPbSCM[2],
					},
				},
			},
		},
		"SCM - filter by model ID": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
				ModelID:      "PartNumber1",
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:       mockSCM,
				UpdateFirmwareErr: nil,
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				ScmResults: []*ctlpb.ScmFirmwareUpdateResp{
					{
						Module: mockPbSCM[1],
					},
				},
			},
		},
		"SCM - specific devices": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_SCM,
				FirmwarePath: "/some/path",
				DeviceIDs:    []string{"Device1", "Device2"},
			},
			smbc: &scm.MockBackendConfig{
				DiscoverRes:       mockSCM,
				UpdateFirmwareErr: nil,
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				ScmResults: []*ctlpb.ScmFirmwareUpdateResp{
					{
						Module: mockPbSCM[1],
					},
					{
						Module: mockPbSCM[2],
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
		"NVMe - filter by FW rev": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_NVMe,
				FirmwarePath: "/some/path",
				FirmwareRev:  "fwRev-0",
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: mockNVMe},
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				NvmeResults: []*ctlpb.NvmeFirmwareUpdateResp{
					{
						PciAddr: mockNVMe[0].PciAddr,
					},
				},
			},
		},
		"NVMe - filter by model ID": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_NVMe,
				FirmwarePath: "/some/path",
				ModelID:      "model-2",
			},
			bmbc: &bdev.MockBackendConfig{
				ScanRes: &bdev.ScanResponse{Controllers: mockNVMe},
			},
			expResp: &ctlpb.FirmwareUpdateResp{
				NvmeResults: []*ctlpb.NvmeFirmwareUpdateResp{
					{
						PciAddr: mockNVMe[2].PciAddr,
					},
				},
			},
		},
		"NVMe - specific devices": {
			req: ctlpb.FirmwareUpdateReq{
				Type:         ctlpb.FirmwareUpdateReq_NVMe,
				FirmwarePath: "/some/path",
				DeviceIDs:    []string{"0000:80:00.0", "0000:80:00.1"},
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
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := config.DefaultServer()
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
