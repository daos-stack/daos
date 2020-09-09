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
		&storage.ScmModule{UID: "Device1"},
		&storage.ScmModule{UID: "Device2"},
		&storage.ScmModule{UID: "Device3"},
	}

	fwInfo := &storage.ScmFirmwareInfo{
		ActiveVersion:     "ACTIVE",
		StagedVersion:     "STAGED",
		ImageMaxSizeBytes: 1 << 20,
		UpdateStatus:      storage.ScmUpdateStatusStaged,
	}

	for name, tc := range map[string]struct {
		inputDevices []string
		backendCfg   *MockBackendConfig
		expErr       error
		expRes       *FirmwareQueryResponse
	}{
		"discovery failed": {
			backendCfg: &MockBackendConfig{DiscoverErr: errors.New("mock discovery")},
			expErr:     errors.New("mock discovery"),
		},
		"no modules": {
			expRes: &FirmwareQueryResponse{
				Results: []ModuleFirmware{},
			},
		},
		"success": {
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
		"request nonexistent": {
			inputDevices: []string{"Device1", "NotReal"},
			backendCfg: &MockBackendConfig{
				DiscoverRes:          defaultModules,
				GetFirmwareStatusRes: fwInfo,
			},
			expErr: errors.New("no module found with UID \"NotReal\""),
		},
		"request device subset": {
			inputDevices: []string{"Device1", "Device3"},
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
		"ignore duplicates": {
			inputDevices: []string{"Device1", "Device3", "Device1"},
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
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.backendCfg, nil)

			res, err := p.QueryFirmware(FirmwareQueryRequest{
				Devices: tc.inputDevices,
			})

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expRes, res); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProvider_UpdateFirmware(t *testing.T) {
	defaultModules := storage.ScmModules{
		&storage.ScmModule{UID: "Device1"},
		&storage.ScmModule{UID: "Device2"},
		&storage.ScmModule{UID: "Device3"},
	}

	testErr := errors.New("test error")
	testPath := "/some/path/file.bin"

	for name, tc := range map[string]struct {
		inputDevices []string
		inputPath    string
		backendCfg   *MockBackendConfig
		expErr       error
		expRes       *FirmwareUpdateResponse
	}{
		"empty path": {
			expErr: errors.New("missing path to firmware file"),
		},
		"discovery failed": {
			inputPath:  testPath,
			backendCfg: &MockBackendConfig{DiscoverErr: errors.New("mock discovery")},
			expErr:     errors.New("mock discovery"),
		},
		"no modules": {
			inputPath: testPath,
			expErr:    errors.New("no SCM modules"),
		},
		"success": {
			inputPath: testPath,
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
					{
						Module: storage.ScmModule{UID: "Device1"},
					},
					{
						Module: storage.ScmModule{UID: "Device2"},
					},
					{
						Module: storage.ScmModule{UID: "Device3"},
					},
				},
			},
		},
		"update failed": {
			inputPath: testPath,
			backendCfg: &MockBackendConfig{
				DiscoverRes:       defaultModules,
				UpdateFirmwareErr: testErr,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
					{
						Module: storage.ScmModule{UID: "Device1"},
						Error:  testErr.Error(),
					},
					{
						Module: storage.ScmModule{UID: "Device2"},
						Error:  testErr.Error(),
					},
					{
						Module: storage.ScmModule{UID: "Device3"},
						Error:  testErr.Error(),
					},
				},
			},
		},
		"request device subset": {
			inputPath:    testPath,
			inputDevices: []string{"Device3", "Device2"},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expRes: &FirmwareUpdateResponse{
				Results: []ModuleFirmwareUpdateResult{
					{
						Module: storage.ScmModule{UID: "Device2"},
					},
					{
						Module: storage.ScmModule{UID: "Device3"},
					},
				},
			},
		},
		"request nonexistent device": {
			inputPath:    testPath,
			inputDevices: []string{"Device3", "NotReal"},
			backendCfg: &MockBackendConfig{
				DiscoverRes: defaultModules,
			},
			expErr: errors.New("no module found with UID \"NotReal\""),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.backendCfg, nil)

			res, err := p.UpdateFirmware(FirmwareUpdateRequest{
				Devices:      tc.inputDevices,
				FirmwarePath: tc.inputPath,
			})

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expRes, res); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
