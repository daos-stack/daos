//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestProvider_QueryFirmware(t *testing.T) {
	defaultModules := storage.ScmModules{
		storage.MockScmModule(1),
		storage.MockScmModule(2),
		storage.MockScmModule(3),
	}

	fwInfo := &storage.ScmFirmwareInfo{
		ActiveVersion:     "ACTIVE",
		StagedVersion:     "STAGED",
		ImageMaxSizeBytes: 1 << 20,
		UpdateStatus:      storage.ScmUpdateStatusStaged,
	}

	for name, tc := range map[string]struct {
		input      storage.ScmFirmwareQueryRequest
		backendCfg *MockBackendConfig
		expErr     error
		expRes     *storage.ScmFirmwareQueryResponse
	}{
		"discovery failed": {
			input:      storage.ScmFirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{DiscoverErr: errors.New("mock discovery")},
			expErr:     errors.New("mock discovery"),
		},
		"no modules": {
			input: storage.ScmFirmwareQueryRequest{},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{},
			},
		},
		"success": {
			input: storage.ScmFirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *defaultModules[0],
						Info:   fwInfo,
					},
					{
						Module: *defaultModules[1],
						Info:   fwInfo,
					},
					{
						Module: *defaultModules[2],
						Info:   fwInfo,
					},
				},
			},
		},
		"get status failed": {
			input: storage.ScmFirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusErr: errors.New("mock query"),
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *defaultModules[0],
						Error:  "mock query",
					},
					{
						Module: *defaultModules[1],
						Error:  "mock query",
					},
					{
						Module: *defaultModules[2],
						Error:  "mock query",
					},
				},
			},
		},
		"request nonexistent is ignored": {
			input: storage.ScmFirmwareQueryRequest{
				DeviceUIDs: []string{"Device1", "NotReal"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *defaultModules[0],
						Info:   fwInfo,
					},
				},
			},
		},
		"request device subset": {
			input: storage.ScmFirmwareQueryRequest{
				DeviceUIDs: []string{"Device1", "Device3"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *defaultModules[0],
						Info:   fwInfo,
					},
					{
						Module: *defaultModules[2],
						Info:   fwInfo,
					},
				},
			},
		},
		"request duplicate devices": {
			input: storage.ScmFirmwareQueryRequest{
				DeviceUIDs: []string{"Device1", "Device3", "Device1"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expErr: FaultDuplicateDevices,
		},
		"filter by FW rev": {
			input: storage.ScmFirmwareQueryRequest{
				FirmwareRev: "FWRev1",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *defaultModules[0],
						Info:   fwInfo,
					},
				},
			},
		},
		"filter by model ID": {
			input: storage.ScmFirmwareQueryRequest{
				ModelID: "PartNumber2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *defaultModules[1],
						Info:   fwInfo,
					},
				},
			},
		},
		"nothing matches both filters": {
			input: storage.ScmFirmwareQueryRequest{
				FirmwareRev: "FWRev1",
				ModelID:     "PartNumber2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{},
			},
		},
		"filter is case insensitive": {
			input: storage.ScmFirmwareQueryRequest{
				ModelID:     "PARTNUMBER2",
				FirmwareRev: "FWREV2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *defaultModules[1],
						Info:   fwInfo,
					},
				},
			},
		},
		"nothing in device list matches filters": {
			input: storage.ScmFirmwareQueryRequest{
				DeviceUIDs:  []string{"Device2", "Device3"},
				FirmwareRev: "FWRev1",
				ModelID:     "PartNumber1",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.backendCfg, nil)

			res, err := p.QueryFirmware(tc.input)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expRes, res); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProvider_UpdateFirmware(t *testing.T) {
	defaultModules := storage.ScmModules{
		storage.MockScmModule(1),
		storage.MockScmModule(2),
		storage.MockScmModule(3),
	}

	testErr := errors.New("test error")
	testPath := "/some/path/file.bin"

	for name, tc := range map[string]struct {
		input      storage.ScmFirmwareUpdateRequest
		backendCfg *MockBackendConfig
		expErr     error
		expRes     *storage.ScmFirmwareUpdateResponse
	}{
		"empty path": {
			expErr: errors.New("missing path to firmware file"),
		},
		"discovery failed": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{DiscoverErr: errors.New("mock discovery")},
			expErr:     errors.New("mock discovery"),
		},
		"no modules": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
			},
			expErr: errors.New("no SCM modules"),
		},
		"success": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &storage.ScmFirmwareUpdateResponse{
				Results: []storage.ScmFirmwareUpdateResult{
					{
						Module: *defaultModules[0],
					},
					{
						Module: *defaultModules[1],
					},
					{
						Module: *defaultModules[2],
					},
				},
			},
		},
		"update failed": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:       defaultModules,
				UpdateFirmwareErr: testErr,
			},
			expRes: &storage.ScmFirmwareUpdateResponse{
				Results: []storage.ScmFirmwareUpdateResult{
					{
						Module: *defaultModules[0],
						Error:  testErr.Error(),
					},
					{
						Module: *defaultModules[1],
						Error:  testErr.Error(),
					},
					{
						Module: *defaultModules[2],
						Error:  testErr.Error(),
					},
				},
			},
		},
		"request device subset": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceUIDs:   []string{"Device3", "Device2"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &storage.ScmFirmwareUpdateResponse{
				Results: []storage.ScmFirmwareUpdateResult{
					{
						Module: *defaultModules[1],
					},
					{
						Module: *defaultModules[2],
					},
				},
			},
		},
		"request nonexistent device": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceUIDs:   []string{"Device3", "NotReal"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expErr: errors.New("no module found with UID \"NotReal\""),
		},
		"request duplicate devices": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceUIDs:   []string{"Device3", "Device3"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expErr: FaultDuplicateDevices,
		},
		"filter by FW rev": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
				FirmwareRev:  "FWRev1",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &storage.ScmFirmwareUpdateResponse{
				Results: []storage.ScmFirmwareUpdateResult{
					{
						Module: *defaultModules[0],
					},
				},
			},
		},
		"filter by model ID": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "PartNumber2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &storage.ScmFirmwareUpdateResponse{
				Results: []storage.ScmFirmwareUpdateResult{
					{
						Module: *defaultModules[1],
					},
				},
			},
		},
		"nothing matches both filters": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
				FirmwareRev:  "FWRev1",
				ModelID:      "PartNumber2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expErr: FaultNoFilterMatch,
		},
		"filter is case insensitive": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "PARTNUMBER2",
				FirmwareRev:  "FWREV2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &storage.ScmFirmwareUpdateResponse{
				Results: []storage.ScmFirmwareUpdateResult{
					{
						Module: *defaultModules[1],
					},
				},
			},
		},
		"nothing in device list matches filters": {
			input: storage.ScmFirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceUIDs:   []string{"Device2", "Device3"},
				FirmwareRev:  "FWRev1",
				ModelID:      "PartNumber1",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expErr: FaultNoFilterMatch,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.backendCfg, nil)

			res, err := p.UpdateFirmware(tc.input)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expRes, res); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
