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
	"github.com/daos-stack/daos/src/control/provider/system"
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

func TestDaosAdmin_MetadataMountHandler(t *testing.T) {
	mountReqPayload, err := json.Marshal(storage.MetadataMountRequest{
		RootPath: "something",
		Device:   "somethingelse",
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		provider   *storage.MockMetadataProvider
		expPayload *storage.MountResponse
		expErr     *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"nil payload": {
			req: &pbin.Request{
				Method: "MetadataMount",
			},
			expErr: nilPayloadErr,
		},
		"mount failed": {
			req: &pbin.Request{
				Method:  "MetadataMount",
				Payload: mountReqPayload,
			},
			provider: &storage.MockMetadataProvider{
				MountErr: errors.New("mock Mount"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("mock Mount"),
		},
		"mount success": {
			req: &pbin.Request{
				Method:  "MetadataMount",
				Payload: mountReqPayload,
			},
			expPayload: &storage.MountResponse{},
		},
		"unmount failed": {
			req: &pbin.Request{
				Method:  "MetadataUnmount",
				Payload: mountReqPayload,
			},
			provider: &storage.MockMetadataProvider{
				UnmountErr: errors.New("mock Unmount"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("mock Unmount"),
		},
		"unmount success": {
			req: &pbin.Request{
				Method:  "MetadataUnmount",
				Payload: mountReqPayload,
			},
			expPayload: &storage.MountResponse{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			if tc.provider == nil {
				tc.provider = &storage.MockMetadataProvider{
					MountRes:   &storage.MountResponse{},
					UnmountRes: &storage.MountResponse{},
				}
			}

			handler := &metadataMountHandler{metadataHandler: metadataHandler{mdProvider: tc.provider}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
			if tc.expPayload == nil {
				tc.expPayload = &storage.MountResponse{}
			}
			expectPayload(t, resp, &storage.MountResponse{}, tc.expPayload)
		})
	}
}

func TestDaosAdmin_MetadataFormatHandler(t *testing.T) {
	reqPayload, err := json.Marshal(storage.MetadataFormatRequest{
		RootPath: "something",
		Device:   "somethingelse",
		DataPath: "datapath",
		OwnerUID: 100,
		OwnerGID: 200,
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req      *pbin.Request
		provider *storage.MockMetadataProvider
		expErr   *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"nil payload": {
			req: &pbin.Request{
				Method: "MetadataFormat",
			},
			expErr: nilPayloadErr,
		},
		"format failed": {
			req: &pbin.Request{
				Method:  "MetadataFormat",
				Payload: reqPayload,
			},
			provider: &storage.MockMetadataProvider{
				FormatErr: errors.New("mock Format"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("mock Format"),
		},
		"format success": {
			req: &pbin.Request{
				Method:  "MetadataFormat",
				Payload: reqPayload,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			if tc.provider == nil {
				tc.provider = &storage.MockMetadataProvider{}
			}

			handler := &metadataFormatHandler{metadataHandler: metadataHandler{mdProvider: tc.provider}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestDaosAdmin_MetadataNeedsFormatHandler(t *testing.T) {
	reqPayload, err := json.Marshal(storage.MetadataFormatRequest{
		RootPath: "something",
		Device:   "somethingelse",
		DataPath: "datapath",
		OwnerUID: 100,
		OwnerGID: 200,
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req      *pbin.Request
		provider *storage.MockMetadataProvider
		expRes   bool
		expErr   *fault.Fault
	}{
		"nil request": {
			expErr: pbin.PrivilegedHelperRequestFailed("nil request"),
		},
		"nil payload": {
			req: &pbin.Request{
				Method: "MetadataNeedsFormat",
			},
			expErr: nilPayloadErr,
		},
		"format check failed": {
			req: &pbin.Request{
				Method:  "MetadataNeedsFormat",
				Payload: reqPayload,
			},
			provider: &storage.MockMetadataProvider{
				NeedsFormatErr: errors.New("mock NeedsFormat"),
			},
			expErr: pbin.PrivilegedHelperRequestFailed("mock NeedsFormat"),
		},
		"format needed": {
			req: &pbin.Request{
				Method:  "MetadataNeedsFormat",
				Payload: reqPayload,
			},
			provider: &storage.MockMetadataProvider{
				NeedsFormatRes: true,
			},
			expRes: true,
		},
		"format not needed": {
			req: &pbin.Request{
				Method:  "MetadataNeedsFormat",
				Payload: reqPayload,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			if tc.provider == nil {
				tc.provider = &storage.MockMetadataProvider{}
			}

			handler := &metadataFormatHandler{metadataHandler: metadataHandler{mdProvider: tc.provider}}

			resp := handler.Handle(log, tc.req)

			if diff := cmp.Diff(tc.expErr, resp.Error); diff != "" {
				t.Errorf("got wrong fault (-want, +got)\n%s\n", diff)
			}

			var result bool
			if err := json.Unmarshal(resp.Payload, &result); err != nil {
				t.Fatal(err)
			}
			test.AssertEqual(t, tc.expRes, result, "")
		})
	}
}

func TestDaosAdmin_ScmMountUnmountHandler(t *testing.T) {
	testTarget, cleanup := test.CreateTestDir(t)
	defer cleanup()

	mountReqPayload, err := json.Marshal(storage.ScmMountRequest{
		Class:  storage.ClassRam,
		Target: testTarget,
		Ramdisk: &storage.RamdiskParams{
			Size: 1024,
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req        *pbin.Request
		smbc       *scm.MockBackendConfig
		smsc       *system.MockSysConfig
		expPayload *storage.MountResponse
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
			expPayload: &storage.MountResponse{Target: testTarget, Mounted: true},
		},
		"ScmMount failure": {
			req: &pbin.Request{
				Method:  "ScmMount",
				Payload: mountReqPayload,
			},
			smsc: &system.MockSysConfig{
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
			expPayload: &storage.MountResponse{Target: testTarget, Mounted: false},
		},
		"ScmUnmount failure": {
			req: &pbin.Request{
				Method:  "ScmUnmount",
				Payload: mountReqPayload,
			},
			smsc: &system.MockSysConfig{
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
				tc.expPayload = &storage.MountResponse{}
			}
			expectPayload(t, resp, &storage.MountResponse{}, tc.expPayload)
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
		Dcpm: &storage.DeviceParams{
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
		smsc       *system.MockSysConfig
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
			smsc: &system.MockSysConfig{
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
		"ScmCheckFormat success; ram": {
			req: &pbin.Request{
				Method:  "ScmCheckFormat",
				Payload: scmFormatReqPayload,
			},
			expPayload: &storage.ScmFormatResponse{Mountpoint: testTarget},
		},
		"ScmCheckFormat success; dcpm": {
			req: &pbin.Request{
				Method:  "ScmCheckFormat",
				Payload: dcpmFormatReqPayload,
			},
			expPayload: &storage.ScmFormatResponse{
				Mountpoint: testTarget,
				Formatted:  true,
			},
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
		smsc       *system.MockSysConfig
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
		"ScmPrepare success; no modules": {
			req: &pbin.Request{
				Method:  "ScmPrepare",
				Payload: scmPrepareReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: []*storage.ScmModule{},
				PrepRes: &storage.ScmPrepareResponse{
					Socket: &storage.ScmSocketState{
						State: storage.ScmNoModules,
					},
					Namespaces: storage.ScmNamespaces{},
				},
			},
			expPayload: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
				Namespaces: storage.ScmNamespaces{},
			},
		},
		"ScmPrepare success; with modules": {
			req: &pbin.Request{
				Method:  "ScmPrepare",
				Payload: scmPrepareReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				GetModulesRes: []*storage.ScmModule{
					storage.MockScmModule(0),
				},
				PrepRes: &storage.ScmPrepareResponse{
					Socket: &storage.ScmSocketState{
						State: storage.ScmFreeCap,
					},
					Namespaces: storage.ScmNamespaces{},
				},
			},
			expPayload: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
				Namespaces: storage.ScmNamespaces{},
			},
		},
		"ScmPrepare failure": {
			req: &pbin.Request{
				Method:  "ScmPrepare",
				Payload: scmPrepareReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				GetModulesErr: errors.New("scan failed"),
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
		smsc       *system.MockSysConfig
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
			expPayload: &storage.ScmScanResponse{
				Namespaces: storage.ScmNamespaces{},
				Modules:    storage.ScmModules{},
			},
		},
		"ScmScan failure": {
			req: &pbin.Request{
				Method:  "ScmScan",
				Payload: scmScanReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				GetModulesErr: errors.New("scan failed"),
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
