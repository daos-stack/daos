//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestProvider_QueryFirmware(t *testing.T) {
	defaultDevs := storage.MockNvmeControllers(3)

	for name, tc := range map[string]struct {
		input      storage.NVMeFirmwareQueryRequest
		backendCfg *MockBackendConfig
		expErr     error
		expRes     *storage.NVMeFirmwareQueryResponse
	}{
		"NVMe device scan failed": {
			input:      storage.NVMeFirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{ScanErr: errors.New("mock scan")},
			expErr:     errors.New("mock scan"),
		},
		"no devices": {
			input:      storage.NVMeFirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{},
			},
		},
		"success": {
			input: storage.NVMeFirmwareQueryRequest{},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{
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
			input: storage.NVMeFirmwareQueryRequest{
				DeviceAddrs: []string{"0000:80:00.0", "0000:80:00.2"},
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{
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
			input: storage.NVMeFirmwareQueryRequest{
				DeviceAddrs: []string{"0000:80:00.0", "fake"},
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[0],
					},
				},
			},
		},
		"request duplicates": {
			input: storage.NVMeFirmwareQueryRequest{
				DeviceAddrs: []string{"0000:80:00.0", "0000:80:00.0"},
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultDuplicateDevices,
		},
		"filter by model ID": {
			input: storage.NVMeFirmwareQueryRequest{
				ModelID: "model-1",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[1],
					},
				},
			},
		},
		"filter by FW rev": {
			input: storage.NVMeFirmwareQueryRequest{
				FirmwareRev: "fwRev-2",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[2],
					},
				},
			},
		},
		"nothing in system matches filters": {
			input: storage.NVMeFirmwareQueryRequest{
				ModelID:     "model-123",
				FirmwareRev: "fwRev-123",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{},
			},
		},
		"must match all requested filters": {
			input: storage.NVMeFirmwareQueryRequest{
				ModelID:     "model-0",
				FirmwareRev: "fwRev-1",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{},
			},
		},
		"case insensitive filters": {
			input: storage.NVMeFirmwareQueryRequest{
				ModelID:     "MODEL-0",
				FirmwareRev: "FWREV-0",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{
					{
						Device: *defaultDevs[0],
					},
				},
			},
		},
		"nothing in device list matches filters": {
			input: storage.NVMeFirmwareQueryRequest{
				DeviceAddrs: []string{"0000:80:00.1", "0000:80:00.2"},
				ModelID:     "model-0",
				FirmwareRev: "fwRev-0",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareQueryResponse{
				Results: []storage.NVMeDeviceFirmwareQueryResult{},
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

func TestProvider_UpdateFirmware(t *testing.T) {
	defaultDevs := storage.MockNvmeControllers(3)

	testErr := errors.New("test error")
	testPath := "/some/path/file.bin"

	for name, tc := range map[string]struct {
		input      storage.NVMeFirmwareUpdateRequest
		backendCfg *MockBackendConfig
		expErr     error
		expRes     *storage.NVMeFirmwareUpdateResponse
	}{
		"empty path": {
			expErr: errors.New("missing path to firmware file"),
		},
		"NVMe device scan failed": {
			input:      storage.NVMeFirmwareUpdateRequest{FirmwarePath: testPath},
			backendCfg: &MockBackendConfig{ScanErr: errors.New("mock scan")},
			expErr:     errors.New("mock scan"),
		},
		"no devices": {
			input:  storage.NVMeFirmwareUpdateRequest{FirmwarePath: testPath},
			expErr: errors.New("no NVMe device controllers"),
		},
		"success": {
			input: storage.NVMeFirmwareUpdateRequest{FirmwarePath: testPath},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareUpdateResponse{
				Results: []storage.NVMeDeviceFirmwareUpdateResult{
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
			input: storage.NVMeFirmwareUpdateRequest{FirmwarePath: testPath},
			backendCfg: &MockBackendConfig{
				ScanRes:   &storage.BdevScanResponse{Controllers: defaultDevs},
				UpdateErr: testErr,
			},
			expRes: &storage.NVMeFirmwareUpdateResponse{
				Results: []storage.NVMeDeviceFirmwareUpdateResult{
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
			input: storage.NVMeFirmwareUpdateRequest{
				DeviceAddrs:  []string{"0000:80:00.0", "0000:80:00.2"},
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareUpdateResponse{
				Results: []storage.NVMeDeviceFirmwareUpdateResult{
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
			input: storage.NVMeFirmwareUpdateRequest{
				DeviceAddrs:  []string{"0000:80:00.0", "fake"},
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expErr: storage.FaultBdevNotFound("fake"),
		},
		"request duplicates": {
			input: storage.NVMeFirmwareUpdateRequest{
				DeviceAddrs:  []string{"0000:80:00.0", "0000:80:00.0"},
				FirmwarePath: testPath,
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultDuplicateDevices,
		},
		"filter by model ID": {
			input: storage.NVMeFirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "model-1",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareUpdateResponse{
				Results: []storage.NVMeDeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[1],
					},
				},
			},
		},
		"filter by FW rev": {
			input: storage.NVMeFirmwareUpdateRequest{
				FirmwarePath: testPath,
				FirmwareRev:  "fwRev-2",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareUpdateResponse{
				Results: []storage.NVMeDeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[2],
					},
				},
			},
		},
		"nothing in system matches filters": {
			input: storage.NVMeFirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "model-123",
				FirmwareRev:  "fwRev-123",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultNoFilterMatch,
		},
		"must match all requested filters": {
			input: storage.NVMeFirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "model-0",
				FirmwareRev:  "fwRev-1",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expErr: FaultNoFilterMatch,
		},
		"case insensitive filters": {
			input: storage.NVMeFirmwareUpdateRequest{
				FirmwarePath: testPath,
				ModelID:      "MODEL-0",
				FirmwareRev:  "FWREV-0",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
			},
			expRes: &storage.NVMeFirmwareUpdateResponse{
				Results: []storage.NVMeDeviceFirmwareUpdateResult{
					{
						Device: *defaultDevs[0],
					},
				},
			},
		},
		"nothing in device list matches filters": {
			input: storage.NVMeFirmwareUpdateRequest{
				FirmwarePath: testPath,
				DeviceAddrs:  []string{"0000:80:00.1", "0000:80:00.2"},
				ModelID:      "model-0",
				FirmwareRev:  "fwRev-0",
			},
			backendCfg: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: defaultDevs},
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

/* todo_tiering
func TestProvider_WithFirmwareForwarder(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	provider := NewProvider(log, DefaultMockBackend())

	if provider.fwFwd == nil {
		t.Fatal("forwarder is nil")
	}
}
todo_tiering */
