//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build firmware
// +build firmware

package main

import (
	"encoding/json"
	"errors"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func expectPayload(t *testing.T, resp *pbin.Response, payload interface{}, expPayload interface{}) {
	t.Helper()

	err := json.Unmarshal(resp.Payload, payload)
	if err != nil {
		t.Fatalf("couldn't unmarshal response payload")
	}

	cmpOpts := []cmp.Option{
		cmp.Comparer(test.CmpErrBool),
	}
	if diff := cmp.Diff(expPayload, payload, cmpOpts...); diff != "" {
		t.Errorf("got wrong payload (-want, +got)\n%s\n", diff)
	}
}

func TestDaosFirmware_ScmQueryHandler(t *testing.T) {
	scmQueryReqPayload, err := json.Marshal(storage.ScmFirmwareQueryRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	testModules := storage.MockScmModules(2)

	testFWInfo := &storage.ScmFirmwareInfo{
		ActiveVersion:     "A100",
		StagedVersion:     "A113",
		ImageMaxSizeBytes: 1024,
		UpdateStatus:      storage.ScmUpdateStatusFailed,
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		expPayload *storage.ScmFirmwareQueryResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"ScmQueryFirmware nil payload": {
			req: &pbin.Request{
				Method: storage.ScmFirmwareQueryMethod,
			},
			expErr: pbin.PrivilegedHelperRequestFailed("unexpected end of JSON input"),
		},
		"ScmQueryFirmware success": {
			req: &pbin.Request{
				Method:  storage.ScmFirmwareQueryMethod,
				Payload: scmQueryReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes:        testModules,
				GetFirmwareStatusRes: testFWInfo,
			},
			expPayload: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *testModules[0],
						Info:   testFWInfo,
					},
					{
						Module: *testModules[1],
						Info:   testFWInfo,
					},
				},
			},
		},
		"ScmQueryFirmware failure": {
			req: &pbin.Request{
				Method:  storage.ScmFirmwareQueryMethod,
				Payload: scmQueryReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes:        testModules,
				GetFirmwareStatusErr: errors.New("mock failure"),
			},
			expPayload: &storage.ScmFirmwareQueryResponse{
				Results: []storage.ScmModuleFirmware{
					{
						Module: *testModules[0],
						Error:  "mock failure",
					},
					{
						Module: *testModules[1],
						Error:  "mock failure",
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, nil)
			handler := &scmQueryHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.ScmFirmwareQueryResponse{}
			}
			expectPayload(t, resp, &storage.ScmFirmwareQueryResponse{}, tc.expPayload)
		})
	}
}

func TestDaosFirmware_ScmUpdateHandler(t *testing.T) {
	scmUpdateReqPayload, err := json.Marshal(storage.ScmFirmwareUpdateRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
		FirmwarePath:       "/some/path",
	})
	if err != nil {
		t.Fatal(err)
	}

	testModules := storage.MockScmModules(3)

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		expPayload *storage.ScmFirmwareUpdateResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"ScmUpdateFirmware nil payload": {
			req: &pbin.Request{
				Method: storage.ScmFirmwareUpdateMethod,
			},
			expErr: pbin.PrivilegedHelperRequestFailed("unexpected end of JSON input"),
		},
		"ScmUpdateFirmware success": {
			req: &pbin.Request{
				Method:  storage.ScmFirmwareUpdateMethod,
				Payload: scmUpdateReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: testModules,
			},
			expPayload: &storage.ScmFirmwareUpdateResponse{
				Results: []storage.ScmFirmwareUpdateResult{
					{Module: *testModules[0]},
					{Module: *testModules[1]},
					{Module: *testModules[2]},
				},
			},
		},
		"ScmUpdateFirmware failure": {
			req: &pbin.Request{
				Method:  storage.ScmFirmwareUpdateMethod,
				Payload: scmUpdateReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes:     testModules,
				UpdateFirmwareErr: errors.New("mock failure"),
			},
			expPayload: &storage.ScmFirmwareUpdateResponse{
				Results: []storage.ScmFirmwareUpdateResult{
					{
						Module: *testModules[0],
						Error:  "mock failure",
					},
					{
						Module: *testModules[1],
						Error:  "mock failure",
					},
					{
						Module: *testModules[2],
						Error:  "mock failure",
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, nil)
			handler := &scmUpdateHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}

			payload := &storage.ScmFirmwareUpdateResponse{}
			err := json.Unmarshal(resp.Payload, payload)
			if err != nil {
				t.Fatalf("couldn't unmarshal response payload")
			}

			if tc.expPayload == nil {
				tc.expPayload = &storage.ScmFirmwareUpdateResponse{}
			}
			if diff := cmp.Diff(tc.expPayload, payload); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDaosFirmware_NvmeUpdateHandler(t *testing.T) {
	updateReqPayload, err := json.Marshal(storage.NVMeFirmwareUpdateRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
		FirmwarePath:       "/some/path",
	})
	if err != nil {
		t.Fatal(err)
	}

	testDevices := storage.MockNvmeControllers(2)

	for name, tc := range map[string]struct {
		req        *pbin.Request
		nmbc       *bdev.MockBackendConfig
		expPayload *storage.NVMeFirmwareUpdateResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"NvmeFirmwareUpdate nil payload": {
			req: &pbin.Request{
				Method: storage.NVMeFirmwareUpdateMethod,
			},
			expErr: pbin.PrivilegedHelperRequestFailed("unexpected end of JSON input"),
		},
		"NvmeFirmwareUpdate success": {
			req: &pbin.Request{
				Method:  storage.NVMeFirmwareUpdateMethod,
				Payload: updateReqPayload,
			},
			nmbc: &bdev.MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{Controllers: testDevices},
			},
			expPayload: &storage.NVMeFirmwareUpdateResponse{
				Results: []storage.NVMeDeviceFirmwareUpdateResult{
					{Device: *testDevices[0]},
					{Device: *testDevices[1]},
				},
			},
		},
		"NvmeFirmwareUpdate failure": {
			req: &pbin.Request{
				Method:  storage.NVMeFirmwareUpdateMethod,
				Payload: updateReqPayload,
			},
			nmbc: &bdev.MockBackendConfig{
				ScanRes:   &storage.BdevScanResponse{Controllers: testDevices},
				UpdateErr: errors.New("mock failure"),
			},
			expPayload: &storage.NVMeFirmwareUpdateResponse{
				Results: []storage.NVMeDeviceFirmwareUpdateResult{
					{
						Device: *testDevices[0],
						Error:  "mock failure",
					},
					{
						Device: *testDevices[1],
						Error:  "mock failure",
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			bp := bdev.NewMockProvider(log, tc.nmbc)
			handler := &nvmeUpdateHandler{bdevHandler: bdevHandler{bdevProvider: bp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}

			payload := &storage.NVMeFirmwareUpdateResponse{}
			err := json.Unmarshal(resp.Payload, payload)
			if err != nil {
				t.Fatalf("couldn't unmarshal response payload")
			}

			if tc.expPayload == nil {
				tc.expPayload = &storage.NVMeFirmwareUpdateResponse{}
			}
			if diff := cmp.Diff(tc.expPayload, payload); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
