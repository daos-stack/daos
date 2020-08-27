//
// (C) Copyright 2019-2020 Intel Corporation.
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
package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
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

	if diff := cmp.Diff(expPayload, payload); diff != "" {
		t.Errorf("got wrong payload (-want, +got)\n%s\n", diff)
	}
}

var nilPayloadErr = pbin.PrivilegedHelperRequestFailed("unexpected end of JSON input")

func TestDaosAdmin_ScmMountUnmountHandler(t *testing.T) {
	testTarget, cleanup := common.CreateTestDir(t)
	defer cleanup()

	mountReqPayload, err := json.Marshal(scm.MountRequest{
		Source: "/src",
		Target: testTarget,
		FsType: "abc",
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		smsc       *scm.MockSysConfig
		expPayload *scm.MountResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"ScmMount nil payload": {
			req: &pbin.Request{
				Method: "ScmMount",
			},
			expErr: nilPayloadErr,
		},
		"ScmMount success": {
			req: &pbin.Request{
				Method:  "ScmMount",
				Payload: mountReqPayload,
			},
			expPayload: &scm.MountResponse{Target: testTarget, Mounted: true},
		},
		"ScmMount failure": {
			req: &pbin.Request{
				Method:  "ScmMount",
				Payload: mountReqPayload,
			},
			smsc: &scm.MockSysConfig{
				MountErr: errors.New("test mount failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed(fmt.Sprintf("mount /src->%s failed: test mount failed", testTarget)),
		},
		"ScmUnmount nil payload": {
			req: &pbin.Request{
				Method: "ScmUnmount",
			},
			expErr: nilPayloadErr,
		},
		"ScmUnmount success": {
			req: &pbin.Request{
				Method:  "ScmUnmount",
				Payload: mountReqPayload,
			},
			expPayload: &scm.MountResponse{Target: testTarget, Mounted: false},
		},
		"ScmUnmount failure": {
			req: &pbin.Request{
				Method:  "ScmUnmount",
				Payload: mountReqPayload,
			},
			smsc: &scm.MockSysConfig{
				UnmountErr: errors.New("test unmount failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed(fmt.Sprintf("failed to unmount %s: test unmount failed", testTarget)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			handler := &scmMountUnmountHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &scm.MountResponse{}
			}
			expectPayload(t, resp, &scm.MountResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_ScmFormatCheckHandler(t *testing.T) {
	testTarget, cleanup := common.CreateTestDir(t)
	defer cleanup()

	scmFormatReqPayload, err := json.Marshal(scm.FormatRequest{
		Mountpoint: testTarget,
		OwnerUID:   os.Getuid(),
		OwnerGID:   os.Getgid(),
		Ramdisk: &scm.RamdiskParams{
			Size: 1,
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	nilPayloadErr := pbin.PrivilegedHelperRequestFailed("unexpected end of JSON input")

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		smsc       *scm.MockSysConfig
		expPayload *scm.FormatResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"ScmFormat nil payload": {
			req: &pbin.Request{
				Method: "ScmFormat",
			},
			expErr: nilPayloadErr,
		},
		"ScmFormat success": {
			req: &pbin.Request{
				Method:  "ScmFormat",
				Payload: scmFormatReqPayload,
			},
			expPayload: &scm.FormatResponse{
				Mountpoint: testTarget,
				Mounted:    true,
				Formatted:  true,
			},
		},
		"ScmFormat failure": {
			req: &pbin.Request{
				Method:  "ScmFormat",
				Payload: scmFormatReqPayload,
			},
			smsc: &scm.MockSysConfig{
				MountErr: errors.New("test mount failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed(fmt.Sprintf("mount tmpfs->%s failed: test mount failed", testTarget)),
		},
		"ScmCheckFormat nil payload": {
			req: &pbin.Request{
				Method: "ScmCheckFormat",
			},
			expErr: nilPayloadErr,
		},
		"ScmCheckFormat success": {
			req: &pbin.Request{
				Method:  "ScmCheckFormat",
				Payload: scmFormatReqPayload,
			},
			expPayload: &scm.FormatResponse{Mountpoint: testTarget},
		},
		"ScmCheckFormat failure": {
			req: &pbin.Request{
				Method:  "ScmFormat",
				Payload: scmFormatReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scan failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("scan failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			handler := &scmFormatCheckHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &scm.FormatResponse{}
			}
			expectPayload(t, resp, &scm.FormatResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_ScmPrepHandler(t *testing.T) {
	scmPrepareReqPayload, err := json.Marshal(scm.PrepareRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		smsc       *scm.MockSysConfig
		expPayload *scm.PrepareResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"ScmPrepare nil payload": {
			req: &pbin.Request{
				Method: "ScmPrepare",
			},
			expErr: pbin.PrivilegedHelperRequestFailed("unexpected end of JSON input"),
		},
		"ScmPrepare success": {
			req: &pbin.Request{
				Method:  "ScmPrepare",
				Payload: scmPrepareReqPayload,
			},
			expPayload: &scm.PrepareResponse{},
		},
		"ScmPrepare failure": {
			req: &pbin.Request{
				Method:  "ScmPrepare",
				Payload: scmPrepareReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scan failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("scan failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			handler := &scmPrepHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &scm.PrepareResponse{}
			}
			expectPayload(t, resp, &scm.PrepareResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_ScmScanHandler(t *testing.T) {
	scmScanReqPayload, err := json.Marshal(scm.ScanRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		smsc       *scm.MockSysConfig
		expPayload *scm.ScanResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"ScmScan nil payload": {
			req: &pbin.Request{
				Method: "ScmScan",
			},
			expErr: nilPayloadErr,
		},
		"ScmScan success": {
			req: &pbin.Request{
				Method:  "ScmScan",
				Payload: scmScanReqPayload,
			},
			expPayload: &scm.ScanResponse{},
		},
		"ScmScan failure": {
			req: &pbin.Request{
				Method:  "ScmScan",
				Payload: scmScanReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scan failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("scan failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			handler := &scmScanHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &scm.ScanResponse{}
			}
			expectPayload(t, resp, &scm.ScanResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_BdevScanHandler(t *testing.T) {
	bdevScanReqPayload, err := json.Marshal(bdev.ScanRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		bmbc       *bdev.MockBackendConfig
		expPayload *bdev.ScanResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"BdevScan nil payload": {
			req: &pbin.Request{
				Method: "BdevScan",
			},
			expErr: nilPayloadErr,
		},
		"BdevScan success": {
			req: &pbin.Request{
				Method:  "BdevScan",
				Payload: bdevScanReqPayload,
			},
			expPayload: &bdev.ScanResponse{},
		},
		"BdevScan failure": {
			req: &pbin.Request{
				Method:  "BdevScan",
				Payload: bdevScanReqPayload,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("scan failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("scan failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			bp := bdev.NewMockProvider(log, tc.bmbc)
			handler := &bdevScanHandler{bdevHandler: bdevHandler{bdevProvider: bp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &bdev.ScanResponse{}
			}
			expectPayload(t, resp, &bdev.ScanResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_BdevPrepHandler(t *testing.T) {
	bdevPrepareReqPayload, err := json.Marshal(bdev.PrepareRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		bmbc       *bdev.MockBackendConfig
		expPayload *bdev.PrepareResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"BdevPrepare nil payload": {
			req: &pbin.Request{
				Method: "BdevPrepare",
			},
			expErr: nilPayloadErr,
		},
		"BdevPrepare success": {
			req: &pbin.Request{
				Method:  "BdevPrepare",
				Payload: bdevPrepareReqPayload,
			},
			expPayload: &bdev.PrepareResponse{},
		},
		"BdevPrepare failure": {
			req: &pbin.Request{
				Method:  "BdevPrepare",
				Payload: bdevPrepareReqPayload,
			},
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("test prepare failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("test prepare failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			bp := bdev.NewMockProvider(log, tc.bmbc)
			handler := &bdevPrepHandler{bdevHandler: bdevHandler{bdevProvider: bp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &bdev.PrepareResponse{}
			}
			expectPayload(t, resp, &bdev.PrepareResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_BdevFormatHandler(t *testing.T) {
	bdevFormatReqPayload, err := json.Marshal(bdev.FormatRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
		Class:              storage.BdevClassNvme,
		DeviceList:         []string{"foo"},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		bmbc       *bdev.MockBackendConfig
		expPayload *bdev.FormatResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"BdevFormat nil payload": {
			req: &pbin.Request{
				Method: "BdevFormat",
			},
			expErr: nilPayloadErr,
		},
		"BdevFormat success": {
			req: &pbin.Request{
				Method:  "BdevFormat",
				Payload: bdevFormatReqPayload,
			},
			bmbc: &bdev.MockBackendConfig{
				FormatRes: &bdev.FormatResponse{
					DeviceResponses: bdev.DeviceFormatResponses{
						"foo": &bdev.DeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expPayload: &bdev.FormatResponse{
				DeviceResponses: bdev.DeviceFormatResponses{
					"foo": &bdev.DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
		"BdevFormat device failure": {
			req: &pbin.Request{
				Method:  "BdevFormat",
				Payload: bdevFormatReqPayload,
			},
			bmbc: &bdev.MockBackendConfig{
				FormatErr: bdev.FaultUnknown,
			},
			expErr: bdev.FaultUnknown,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			bp := bdev.NewMockProvider(log, tc.bmbc)
			handler := &bdevFormatHandler{bdevHandler: bdevHandler{bdevProvider: bp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &bdev.FormatResponse{}
			}
			expectPayload(t, resp, &bdev.FormatResponse{}, tc.expPayload)
		})
	}
}
