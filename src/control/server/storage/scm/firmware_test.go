//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build firmware

package scm

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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
		input      FirmwareQueryRequest
		backendCfg *MockBackendConfig
		expErr     error
		expRes     *FirmwareQueryResponse
	}{
		"discovery failed": {
			input:      FirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{DiscoverErr: errors.New("mock discovery")},
			expErr:     errors.New("mock discovery"),
		},
		"no modules": {
			input: FirmwareQueryRequest{},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{},
			},
		},
		"success": {
			input: FirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{
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
			input: FirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusErr: errors.New("mock query"),
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{
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
			input: FirmwareQueryRequest{
				DeviceUIDs: []string{"Device1", "NotReal"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{
					{
						Module: *defaultModules[0],
						Info:   fwInfo,
					},
				},
			},
		},
		"request device subset": {
			input: FirmwareQueryRequest{
				DeviceUIDs: []string{"Device1", "Device3"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{
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
			input: FirmwareQueryRequest{
				DeviceUIDs: []string{"Device1", "Device3", "Device1"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expErr: FaultDuplicateDevices,
		},
		"filter by FW rev": {
			input: FirmwareQueryRequest{
				FirmwareRev: "FWRev1",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{
					{
						Module: *defaultModules[0],
						Info:   fwInfo,
					},
				},
			},
		},
		"filter by model ID": {
			input: FirmwareQueryRequest{
				ModelID: "PartNumber2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{
					{
						Module: *defaultModules[1],
						Info:   fwInfo,
					},
				},
			},
		},
		"nothing matches both filters": {
			input: FirmwareQueryRequest{
				FirmwareRev: "FWRev1",
				ModelID:     "PartNumber2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{},
			},
		},
		"filter is case insensitive": {
			input: FirmwareQueryRequest{
				ModelID:     "PARTNUMBER2",
				FirmwareRev: "FWREV2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{
					{
						Module: *defaultModules[1],
						Info:   fwInfo,
					},
				},
			},
		},
		"nothing in device list matches filters": {
			input: FirmwareQueryRequest{
				DeviceUIDs:  []string{"Device2", "Device3"},
				FirmwareRev: "FWRev1",
				ModelID:     "PartNumber1",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.backendCfg, nil)

			res, err := p.QueryFirmware(tc.input)

			common.CmpErr(t, tc.expErr, err)
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
		input      FirmwareUpdateRequest
		backendCfg *MockBackendConfig
		expErr     error
		expRes     *FirmwareUpdateResponse
	}{
		"empty path": {
			expErr: errors.New("missing path to firmware file"),
		},
		"discovery failed": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{DiscoverErr: errors.New("mock discovery")},
			expErr:     errors.New("mock discovery"),
		},
		"no modules": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
			},
			expErr: errors.New("no SCM modules"),
		},
		"success": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
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
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes:       defaultModules,
				UpdateFirmwareErr: testErr,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
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
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceUIDs:   []string{"Device3", "Device2"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
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
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceUIDs:   []string{"Device3", "NotReal"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expErr: errors.New("no module found with UID \"NotReal\""),
		},
		"request duplicate devices": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceUIDs:   []string{"Device3", "Device3"},
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expErr: FaultDuplicateDevices,
		},
		"filter by FW rev": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				FirmwareRev:  "FWRev1",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
					{
						Module: *defaultModules[0],
					},
				},
			},
		},
		"filter by model ID": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "PartNumber2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
					{
						Module: *defaultModules[1],
					},
				},
			},
		},
		"nothing matches both filters": {
			input: FirmwareUpdateRequest{
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
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "PARTNUMBER2",
				FirmwareRev:  "FWREV2",
			},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
					{
						Module: *defaultModules[1],
					},
				},
			},
		},
		"nothing in device list matches filters": {
			input: FirmwareUpdateRequest{
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
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.backendCfg, nil)

			res, err := p.UpdateFirmware(tc.input)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expRes, res); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
