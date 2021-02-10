//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build firmware

package bdev

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestBdevProvider_QueryFirmware(t *testing.T) {
	defaultDevs := storage.MockNvmeControllers(3)

	for name, tc := range map[string]struct {
		input      FirmwareQueryRequest
		backendCfg *MockBackendConfig
		expErr     error
		expRes     *FirmwareQueryResponse
	}{
		"NVMe device scan failed": {
			input:      FirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{ScanErr: errors.New("mock scan")},
			expErr:     errors.New("mock scan"),
		},
		"no devices": {
			input:      FirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{},
			},
		},
		"success": {
			input: FirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[0],
					},
					{
						Device: *defaultDevs[1],
					},
					{
						Device: *defaultDevs[2],
					},
				},
			},
		},
		"request device subset": {
			input: FirmwareQueryRequest{
				DeviceAddrs: []string{"0000:80:00.0", "0000:80:00.2"},
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[0],
					},
					{
						Device: *defaultDevs[2],
					},
				},
			},
		},
		"request nonexistent device - ignored": {
			input: FirmwareQueryRequest{
				DeviceAddrs: []string{"0000:80:00.0", "fake"},
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[0],
					},
				},
			},
		},
		"request duplicates": {
			input: FirmwareQueryRequest{
				DeviceAddrs: []string{"0000:80:00.0", "0000:80:00.0"},
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultDuplicateDevices,
		},
		"filter by model ID": {
			input: FirmwareQueryRequest{
				ModelID: "model-1",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[1],
					},
				},
			},
		},
		"filter by FW rev": {
			input: FirmwareQueryRequest{
				FirmwareRev: "fwRev-2",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[2],
					},
				},
			},
		},
		"nothing in system matches filters": {
			input: FirmwareQueryRequest{
				ModelID:     "model-123",
				FirmwareRev: "fwRev-123",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{},
			},
		},
		"must match all requested filters": {
			input: FirmwareQueryRequest{
				ModelID:     "model-0",
				FirmwareRev: "fwRev-1",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{},
			},
		},
		"case insensitive filters": {
			input: FirmwareQueryRequest{
				ModelID:     "MODEL-0",
				FirmwareRev: "FWREV-0",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[0],
					},
				},
			},
		},
		"nothing in device list matches filters": {
			input: FirmwareQueryRequest{
				DeviceAddrs: []string{"0000:80:00.1", "0000:80:00.2"},
				ModelID:     "model-0",
				FirmwareRev: "fwRev-0",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareQueryResponse{
				Results: []DeviceFirmwareQueryResult{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.backendCfg)

			res, err := p.QueryFirmware(tc.input)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expRes, res); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBdevProvider_UpdateFirmware(t *testing.T) {
	defaultDevs := storage.MockNvmeControllers(3)

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
		"NVMe device scan failed": {
			input:      FirmwareUpdateRequest{FirmwarePath: testPath},
			backendCfg: &MockBackendConfig{ScanErr: errors.New("mock scan")},
			expErr:     errors.New("mock scan"),
		},
		"no devices": {
			input:  FirmwareUpdateRequest{FirmwarePath: testPath},
			expErr: errors.New("no NVMe device controllers"),
		},
		"success": {
			input: FirmwareUpdateRequest{FirmwarePath: testPath},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareUpdateResponse{
				Results: []DeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[0],
					},
					{
						Device: *defaultDevs[1],
					},
					{
						Device: *defaultDevs[2],
					},
				},
			},
		},
		"update failed": {
			input: FirmwareUpdateRequest{FirmwarePath: testPath},
			backendCfg: &MockBackendConfig{
				ScanRes:   &ScanResponse{Controllers: defaultDevs},
				UpdateErr: testErr,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []DeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[0],
						Error:  testErr.Error(),
					},
					{
						Device: *defaultDevs[1],
						Error:  testErr.Error(),
					},
					{
						Device: *defaultDevs[2],
						Error:  testErr.Error(),
					},
				},
			},
		},
		"request device subset": {
			input: FirmwareUpdateRequest{
				DeviceAddrs:  []string{"0000:80:00.0", "0000:80:00.2"},
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareUpdateResponse{
				Results: []DeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[0],
					},
					{
						Device: *defaultDevs[2],
					},
				},
			},
		},
		"request nonexistent device": {
			input: FirmwareUpdateRequest{
				DeviceAddrs:  []string{"0000:80:00.0", "fake"},
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultPCIAddrNotFound("fake"),
		},
		"request duplicates": {
			input: FirmwareUpdateRequest{
				DeviceAddrs:  []string{"0000:80:00.0", "0000:80:00.0"},
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultDuplicateDevices,
		},
		"filter by model ID": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "model-1",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareUpdateResponse{
				Results: []DeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[1],
					},
				},
			},
		},
		"filter by FW rev": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				FirmwareRev:  "fwRev-2",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareUpdateResponse{
				Results: []DeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[2],
					},
				},
			},
		},
		"nothing in system matches filters": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "model-123",
				FirmwareRev:  "fwRev-123",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultNoFilterMatch,
		},
		"must match all requested filters": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "model-0",
				FirmwareRev:  "fwRev-1",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultNoFilterMatch,
		},
		"case insensitive filters": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "MODEL-0",
				FirmwareRev:  "FWREV-0",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expRes: &FirmwareUpdateResponse{
				Results: []DeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[0],
					},
				},
			},
		},
		"nothing in device list matches filters": {
			input: FirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceAddrs:  []string{"0000:80:00.1", "0000:80:00.2"},
				ModelID:      "model-0",
				FirmwareRev:  "fwRev-0",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &ScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultNoFilterMatch,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.backendCfg)

			res, err := p.UpdateFirmware(tc.input)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expRes, res); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBdevProvider_WithFirmwareForwarder(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	provider := NewProvider(log, DefaultMockBackend())

	if provider.fwFwd == nil {
		t.Fatal("forwarder is nil")
	}
}
