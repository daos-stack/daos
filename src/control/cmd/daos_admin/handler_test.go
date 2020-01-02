//
// (C) Copyright 2019 Intel Corporation.
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
	"bytes"
	"encoding/json"
	"io/ioutil"
	"math/rand"
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func parseResponse(t *testing.T, buf *bytes.Buffer) *pbin.Response {
	t.Helper()

	var res pbin.Response
	if err := json.Unmarshal(buf.Bytes(), &res); err != nil {
		t.Fatalf("%s: %q", err, buf.String())
	}
	return &res
}

func TestHandler(t *testing.T) {
	testTarget, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(testTarget)

	nilPayloadResp := &pbin.Response{
		Error: &pbin.RequestFailure{Message: "unexpected end of JSON input"},
	}
	successResp := &pbin.Response{}
	mountReqPayload, err := json.Marshal(scm.MountRequest{
		Source: "/src",
		Target: testTarget,
		FsType: "abc",
	})
	if err != nil {
		t.Fatal(err)
	}
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

	scmPrepareReqPayload, err := json.Marshal(scm.PrepareRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	scmScanReqPayload, err := json.Marshal(scm.ScanRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	bdevInitReqPayload, err := json.Marshal(bdev.InitRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	bdevScanReqPayload, err := json.Marshal(bdev.ScanRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	bdevPrepareReqPayload, err := json.Marshal(bdev.PrepareRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
	})
	if err != nil {
		t.Fatal(err)
	}

	bdevFormatReqPayload, err := json.Marshal(bdev.FormatRequest{
		ForwardableRequest: pbin.ForwardableRequest{Forwarded: true},
		Class:              storage.BdevClassNvme,
		DeviceList:         []string{"foo"},
	})
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		req    *pbin.Request
		smbc   *scm.MockBackendConfig
		smsc   *scm.MockSysConfig
		bmbc   *bdev.MockBackendConfig
		expRes *pbin.Response
		expErr error
	}{
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"unknown method": {
			req: &pbin.Request{
				Method: "OopsKaboom",
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: `unhandled method "OopsKaboom"`},
			},
		},
		"ScmMount nil payload": {
			req: &pbin.Request{
				Method: "ScmMount",
			},
			expRes: nilPayloadResp,
		},
		"ScmMount success": {
			req: &pbin.Request{
				Method:  "ScmMount",
				Payload: mountReqPayload,
			},
			expRes: successResp,
		},
		"ScmMount failure": {
			req: &pbin.Request{
				Method:  "ScmMount",
				Payload: mountReqPayload,
			},
			smsc: &scm.MockSysConfig{
				MountErr: errors.New("mount failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "mount failed"},
			},
		},
		"ScmUnmount nil payload": {
			req: &pbin.Request{
				Method: "ScmUnmount",
			},
			expRes: nilPayloadResp,
		},
		"ScmUnmount success": {
			req: &pbin.Request{
				Method:  "ScmUnmount",
				Payload: mountReqPayload,
			},
			expRes: successResp,
		},
		"ScmUnmount failure": {
			req: &pbin.Request{
				Method:  "ScmUnmount",
				Payload: mountReqPayload,
			},
			smsc: &scm.MockSysConfig{
				UnmountErr: errors.New("unmount failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "unmount failed"},
			},
		},
		"ScmFormat nil payload": {
			req: &pbin.Request{
				Method: "ScmFormat",
			},
			expRes: nilPayloadResp,
		},
		"ScmFormat success": {
			req: &pbin.Request{
				Method:  "ScmFormat",
				Payload: scmFormatReqPayload,
			},
			expRes: successResp,
		},
		"ScmFormat failure": {
			req: &pbin.Request{
				Method:  "ScmFormat",
				Payload: scmFormatReqPayload,
			},
			smsc: &scm.MockSysConfig{
				MountErr: errors.New("mount failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "mount failed"},
			},
		},
		"ScmCheckFormat nil payload": {
			req: &pbin.Request{
				Method: "ScmCheckFormat",
			},
			expRes: nilPayloadResp,
		},
		"ScmCheckFormat success": {
			req: &pbin.Request{
				Method:  "ScmCheckFormat",
				Payload: scmFormatReqPayload,
			},
			expRes: successResp,
		},
		"ScmCheckFormat failure": {
			req: &pbin.Request{
				Method:  "ScmFormat",
				Payload: scmFormatReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scan failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "scan failed"},
			},
		},
		"ScmPrepare nil payload": {
			req: &pbin.Request{
				Method: "ScmPrepare",
			},
			expRes: nilPayloadResp,
		},
		"ScmPrepare success": {
			req: &pbin.Request{
				Method:  "ScmPrepare",
				Payload: scmPrepareReqPayload,
			},
			expRes: successResp,
		},
		"ScmPrepare failure": {
			req: &pbin.Request{
				Method:  "ScmPrepare",
				Payload: scmPrepareReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scan failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "scan failed"},
			},
		},
		"ScmScan nil payload": {
			req: &pbin.Request{
				Method: "ScmScan",
			},
			expRes: nilPayloadResp,
		},
		"ScmScan success": {
			req: &pbin.Request{
				Method:  "ScmScan",
				Payload: scmScanReqPayload,
			},
			expRes: successResp,
		},
		"ScmScan failure": {
			req: &pbin.Request{
				Method:  "ScmScan",
				Payload: scmScanReqPayload,
			},
			smbc: &scm.MockBackendConfig{
				DiscoverErr: errors.New("scan failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "scan failed"},
			},
		},
		"BdevInit nil payload": {
			req: &pbin.Request{
				Method: "BdevInit",
			},
			expRes: nilPayloadResp,
		},
		"BdevInit success": {
			req: &pbin.Request{
				Method:  "BdevInit",
				Payload: bdevInitReqPayload,
			},
			expRes: successResp,
		},
		"BdevInit failure": {
			req: &pbin.Request{
				Method:  "BdevInit",
				Payload: bdevInitReqPayload,
			},
			bmbc: &bdev.MockBackendConfig{
				InitErr: errors.New("init failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "init failed"},
			},
		},
		"BdevScan nil payload": {
			req: &pbin.Request{
				Method: "BdevScan",
			},
			expRes: nilPayloadResp,
		},
		"BdevScan success": {
			req: &pbin.Request{
				Method:  "BdevScan",
				Payload: bdevScanReqPayload,
			},
			expRes: successResp,
		},
		"BdevScan failure": {
			req: &pbin.Request{
				Method:  "BdevScan",
				Payload: bdevScanReqPayload,
			},
			bmbc: &bdev.MockBackendConfig{
				ScanErr: errors.New("scan failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "scan failed"},
			},
		},
		"BdevPrepare nil payload": {
			req: &pbin.Request{
				Method: "BdevPrepare",
			},
			expRes: nilPayloadResp,
		},
		"BdevPrepare success": {
			req: &pbin.Request{
				Method:  "BdevPrepare",
				Payload: bdevPrepareReqPayload,
			},
			expRes: successResp,
		},
		"BdevPrepare failure": {
			req: &pbin.Request{
				Method:  "BdevPrepare",
				Payload: bdevPrepareReqPayload,
			},
			bmbc: &bdev.MockBackendConfig{
				PrepareErr: errors.New("prepare failed"),
			},
			expRes: &pbin.Response{
				Error: &pbin.RequestFailure{Message: "prepare failed"},
			},
		},
		"BdevFormat nil payload": {
			req: &pbin.Request{
				Method: "BdevFormat",
			},
			expRes: nilPayloadResp,
		},
		"BdevFormat success": {
			req: &pbin.Request{
				Method:  "BdevFormat",
				Payload: bdevFormatReqPayload,
			},
			expRes: successResp,
		},
		"BdevFormat failure": {
			req: &pbin.Request{
				Method:  "BdevFormat",
				Payload: bdevFormatReqPayload,
			},
			bmbc: &bdev.MockBackendConfig{
				FormatErr: errors.New("format failed"),
			},
			expRes: successResp, // The format error is handled at a higher layer
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			var destBuf bytes.Buffer
			sp := scm.NewMockProvider(log, tc.smbc, tc.smsc)
			bp := bdev.NewMockProvider(log, tc.bmbc)

			gotErr := handleRequest(log, sp, bp, tc.req, &destBuf)
			common.CmpErr(t, tc.expErr, gotErr)

			if destBuf.Len() > 0 {
				gotRes := parseResponse(t, &destBuf)

				// We don't need to check the payload details here; we just
				// want to check that a response came back and that the Error
				// field was what we expected.
				common.CmpErr(t, tc.expRes.Error, gotRes.Error)
			}
		})
	}
}

func TestReadRequest(t *testing.T) {
	alnum := []byte("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
	giantPayload := make([]byte, pbin.MaxMessageSize)
	for i := 0; i < len(giantPayload); i++ {
		giantPayload[i] = alnum[rand.Intn(len(alnum))]
	}

	for name, tc := range map[string]struct {
		req    *pbin.Request
		expErr error
	}{
		"normal payload": {
			req: &pbin.Request{
				Method:  "whatever",
				Payload: []byte(`{"foo":"bar"}`),
			},
		},
		"giant payload": {
			req: &pbin.Request{
				Method:  "too big to fail",
				Payload: append(append([]byte(`{"foo":"`), giantPayload...), []byte(`"}`)...),
			},
			expErr: errors.New("unexpected end of JSON input"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			data, err := json.Marshal(tc.req)
			if err != nil {
				t.Fatal(err)
			}

			gotReq, gotErr := readRequest(log, bytes.NewBuffer(data))
			common.CmpErr(t, tc.expErr, gotErr)

			if diff := cmp.Diff(tc.req, gotReq); gotErr == nil && diff != "" {
				t.Fatalf("request did not survive marshal/unmarshal (-want, +got):\n%s\n", diff)
			}
		})
	}
}
