//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
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

	if diff := cmp.Diff(expPayload, payload); diff != "" {
		t.Errorf("got wrong payload (-want, +got)\n%s\n", diff)
	}
}

var nilPayloadErr = pbin.PrivilegedHelperRequestFailed("unexpected end of JSON input")

func TestDaosAdmin_ScmMountUnmountHandler(t *testing.T) {
	testTarget, cleanup := test.CreateTestDir(t)
	defer cleanup()

	mountReqPayload, err := json.Marshal(storage.ScmMountRequest{
		Class:  storage.ClassRam,
		Target: testTarget,
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		smsc       *scm.MockSysConfig
		expPayload *storage.ScmMountResponse
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
			expPayload: &storage.ScmMountResponse{Target: testTarget, Mounted: true},
		},
		"ScmMount failure": {
			req: &pbin.Request{
				Method:  "ScmMount",
				Payload: mountReqPayload,
			},
			smsc: &scm.MockSysConfig{
				MountErr: errors.New("test mount failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed(fmt.Sprintf("mount tmpfs->%s failed: test mount failed", testTarget)),
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
			expPayload: &storage.ScmMountResponse{Target: testTarget, Mounted: false},
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
			defer test.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			handler := &scmMountUnmountHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.ScmMountResponse{}
			}
			expectPayload(t, resp, &storage.ScmMountResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_ScmFormatCheckHandler(t *testing.T) {
	testTarget, cleanup := test.CreateTestDir(t)
	defer cleanup()

	scmFormatReqPayload, err := json.Marshal(storage.ScmFormatRequest{
		Mountpoint: testTarget,
		OwnerUID:   os.Getuid(),
		OwnerGID:   os.Getgid(),
		Ramdisk: &storage.RamdiskParams{
			Size: 1,
		},
	})
	if err != nil {
		t.Fatal(err)
	}
	dcpmFormatReqPayload, err := json.Marshal(storage.ScmFormatRequest{
		Mountpoint: testTarget,
		OwnerUID:   os.Getuid(),
		OwnerGID:   os.Getgid(),
		Dcpm: &storage.DcpmParams{
			Device: "/foo/bar",
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
		expPayload *storage.ScmFormatResponse
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
			expPayload: &storage.ScmFormatResponse{
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
			expPayload: &storage.ScmFormatResponse{Mountpoint: testTarget},
		},
		"ScmCheckFormat scan failure": {
			req: &pbin.Request{
				Method:  "ScmFormat",
				Payload: dcpmFormatReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scan failed"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("scan failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			handler := &scmFormatCheckHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.ScmFormatResponse{}
			}
			expectPayload(t, resp, &storage.ScmFormatResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_ScmPrepHandler(t *testing.T) {
	scmPrepareReqPayload, err := json.Marshal(storage.ScmPrepareRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		smsc       *scm.MockSysConfig
		expPayload *storage.ScmPrepareResponse
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
			expPayload: &storage.ScmPrepareResponse{},
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
			defer test.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			handler := &scmPrepHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.ScmPrepareResponse{}
			}
			expectPayload(t, resp, &storage.ScmPrepareResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_ScmScanHandler(t *testing.T) {
	scmScanReqPayload, err := json.Marshal(storage.ScmScanRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		smsc       *scm.MockSysConfig
		expPayload *storage.ScmScanResponse
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
			expPayload: &storage.ScmScanResponse{},
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
			defer test.ShowBufferOnFailure(t, buf)

			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			handler := &scmScanHandler{scmHandler: scmHandler{scmProvider: sp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.ScmScanResponse{}
			}
			expectPayload(t, resp, &storage.ScmScanResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_BdevScanHandler(t *testing.T) {
	bdevScanReqPayload, err := json.Marshal(storage.BdevScanRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		bmbc       *bdev.MockBackendConfig
		expPayload *storage.BdevScanResponse
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
			expPayload: &storage.BdevScanResponse{},
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
			defer test.ShowBufferOnFailure(t, buf)

			bp := bdev.NewMockProvider(log, tc.bmbc)
			handler := &bdevScanHandler{bdevHandler: bdevHandler{bdevProvider: bp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.BdevScanResponse{}
			}
			expectPayload(t, resp, &storage.BdevScanResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_BdevPrepHandler(t *testing.T) {
	bdevPrepareReqPayload, err := json.Marshal(storage.BdevPrepareRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		bmbc       *bdev.MockBackendConfig
		expPayload *storage.BdevPrepareResponse
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
			expPayload: &storage.BdevPrepareResponse{},
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
			defer test.ShowBufferOnFailure(t, buf)

			bp := bdev.NewMockProvider(log, tc.bmbc)
			handler := &bdevPrepHandler{bdevHandler: bdevHandler{bdevProvider: bp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.BdevPrepareResponse{}
			}
			expectPayload(t, resp, &storage.BdevPrepareResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_BdevFormatHandler(t *testing.T) {
	bdevFormatReqPayload, err := json.Marshal(storage.BdevFormatRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
		Properties: storage.BdevTierProperties{
			Class:      storage.ClassNvme,
			DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddr(1)),
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		bmbc       *bdev.MockBackendConfig
		expPayload *storage.BdevFormatResponse
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
				FormatRes: &storage.BdevFormatResponse{
					DeviceResponses: storage.BdevDeviceFormatResponses{
						"foo": &storage.BdevDeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expPayload: &storage.BdevFormatResponse{
				DeviceResponses: storage.BdevDeviceFormatResponses{
					"foo": &storage.BdevDeviceFormatResponse{
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
			defer test.ShowBufferOnFailure(t, buf)

			bp := bdev.NewMockProvider(log, tc.bmbc)
			handler := &bdevFormatHandler{bdevHandler: bdevHandler{bdevProvider: bp}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.BdevFormatResponse{}
			}
			expectPayload(t, resp, &storage.BdevFormatResponse{}, tc.expPayload)
		})
	}
}
