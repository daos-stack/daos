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
package bdev

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestBdev_ScanResponse_filter(t *testing.T) {
	ctrlr1 := storage.MockNvmeController(1)
	ctrlr2 := storage.MockNvmeController(2)
	ctrlr3 := storage.MockNvmeController(3)
	ctrlr4 := storage.MockNvmeController(4)
	ctrlr5 := storage.MockNvmeController(5)

	for name, tc := range map[string]struct {
		scanResp   *ScanResponse
		deviceList []string
		expResp    *ScanResponse
		expNum     int
	}{
		"scan response no filter": {
			scanResp: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
			expResp: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
		},
		"scan response filtered": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr3.PciAddr},
			scanResp: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
			expResp: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr3},
			},
			expNum: 1,
		},
		"scan response inclusive filter": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr2.PciAddr, ctrlr3.PciAddr},
			scanResp: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr3},
			},
			expResp: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr3},
			},
		},
		"scan response exclusive filter": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr5.PciAddr, ctrlr3.PciAddr},
			scanResp: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr2, ctrlr4},
			},
			expResp: &ScanResponse{
				Controllers: storage.NvmeControllers{},
			},
			expNum: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotNum, gotResp := tc.scanResp.filter(tc.deviceList...)

			common.AssertEqual(t, tc.expNum, gotNum, name+" expected number filtered")
			if diff := cmp.Diff(tc.expResp, gotResp, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBdev_forwardScan(t *testing.T) {
	for name, tc := range map[string]struct {
		scanReq      ScanRequest
		cache        *ScanResponse
		scanResp     *ScanResponse
		scanErr      error
		shouldUpdate bool
		expMsg       string
		expResp      *ScanResponse
		expErr       error
	}{
		"scan error": {
			scanReq: ScanRequest{},
			scanErr: errors.New("fail"),
			expErr:  errors.New("fail"),
		},
		"nil scan response": {
			scanReq:  ScanRequest{},
			scanResp: nil,
			expErr:   errors.New("unexpected nil response from bdev backend"),
		},
		"nil devices": {
			scanReq:      ScanRequest{},
			scanResp:     new(ScanResponse),
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (0 devices)",
			expResp:      new(ScanResponse),
		},
		"no devices": {
			scanReq: ScanRequest{},
			scanResp: &ScanResponse{
				Controllers: storage.NvmeControllers{},
			},
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (0 devices)",
			expResp: &ScanResponse{
				Controllers: storage.NvmeControllers{},
			},
		},
		"update cache": {
			scanReq: ScanRequest{},
			scanResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (3 devices)",
			expResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
		},
		"update empty cache": {
			scanReq: ScanRequest{},
			cache:   &ScanResponse{},
			scanResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (3 devices)",
			expResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
		},
		"reuse cache": {
			scanReq: ScanRequest{},
			cache: &ScanResponse{
				Controllers: storage.MockNvmeControllers(2),
			},
			scanResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			expMsg: "bdev scan: reuse cache (2 devices)",
			expResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(2),
			},
		},
		"bypass cache": {
			scanReq: ScanRequest{NoCache: true},
			cache: &ScanResponse{
				Controllers: storage.MockNvmeControllers(2),
			},
			scanResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			expMsg: "bdev scan: bypass cache (3 devices)",
			expResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
		},
		"filtered devices": {
			scanReq: ScanRequest{
				DeviceList: []string{
					storage.MockNvmeController(0).PciAddr,
					storage.MockNvmeController(1).PciAddr,
				},
			},
			scanResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (3-1 filtered devices)",
			expResp: &ScanResponse{
				Controllers: storage.MockNvmeControllers(2),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			scanFn := func(r ScanRequest) (*ScanResponse, error) {
				return tc.scanResp, tc.scanErr
			}

			gotMsg, gotResp, shouldUpdate, gotErr := forwardScan(tc.scanReq, tc.cache, scanFn)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			common.AssertEqual(t, tc.shouldUpdate, shouldUpdate, name)
			if diff := cmp.Diff(tc.expMsg, gotMsg, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected message (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expResp, gotResp, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBdevScan(t *testing.T) {
	ctrlr1 := storage.MockNvmeController(1)
	ctrlr2 := storage.MockNvmeController(2)
	ctrlr3 := storage.MockNvmeController(3)

	for name, tc := range map[string]struct {
		req            ScanRequest
		forwarded      bool
		mbc            *MockBackendConfig
		expRes         *ScanResponse
		expErr         error
		expVMDDisabled bool
	}{
		"no devices": {
			req:            ScanRequest{},
			expRes:         &ScanResponse{},
			expVMDDisabled: true, // disabled in mock by default
		},
		"single device": {
			req: ScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: &ScanResponse{
					Controllers: storage.NvmeControllers{ctrlr1},
				},
				VmdEnabled: true,
			},
			expRes: &ScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1},
			},
		},
		"multiple devices": {
			req: ScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: &ScanResponse{
					Controllers: storage.NvmeControllers{
						ctrlr1, ctrlr2, ctrlr3,
					},
				},
			},
			expRes: &ScanResponse{
				Controllers: storage.NvmeControllers{
					ctrlr1, ctrlr2, ctrlr3,
				},
			},
			expVMDDisabled: true,
		},
		"multiple devices with vmd disabled": {
			req:       ScanRequest{DisableVMD: true},
			forwarded: true,
			mbc: &MockBackendConfig{
				ScanRes: &ScanResponse{
					Controllers: storage.NvmeControllers{
						ctrlr1, ctrlr2, ctrlr3,
					},
				},
			},
			expRes: &ScanResponse{
				Controllers: storage.NvmeControllers{
					ctrlr1, ctrlr2, ctrlr3,
				},
			},
			expVMDDisabled: true,
		},
		"failure": {
			req: ScanRequest{},
			mbc: &MockBackendConfig{
				ScanErr: errors.New("scan failed"),
			},
			expErr: errors.New("scan failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.mbc)

			tc.req.Forwarded = tc.forwarded

			gotRes, gotErr := p.Scan(tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expRes, gotRes, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
			common.AssertEqual(t, tc.expVMDDisabled, p.IsVMDDisabled(), "vmd disabled")
		})
	}
}

func TestBdevPrepare(t *testing.T) {
	for name, tc := range map[string]struct {
		req           PrepareRequest
		shouldForward bool
		mbc           *MockBackendConfig
		vmdDetectErr  error
		expRes        *PrepareResponse
		expErr        error
	}{
		"reset fails": {
			req: PrepareRequest{},
			mbc: &MockBackendConfig{
				PrepareResetErr: errors.New("reset failed"),
			},
			expErr: errors.New("reset failed"),
		},
		"reset-only": {
			req: PrepareRequest{
				ResetOnly: true,
			},
			mbc: &MockBackendConfig{
				PrepareErr: errors.New("should not get this far"),
			},
			expRes: &PrepareResponse{},
		},
		"prepare fails": {
			req: PrepareRequest{},
			mbc: &MockBackendConfig{
				PrepareErr: errors.New("prepare failed"),
			},
			expErr: errors.New("prepare failed"),
		},
		"prepare succeeds": {
			req:    PrepareRequest{},
			expRes: &PrepareResponse{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.mbc)

			gotRes, gotErr := p.Prepare(tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expRes, gotRes); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBdevFormat(t *testing.T) {
	mockSingle := storage.MockNvmeController()

	for name, tc := range map[string]struct {
		req    FormatRequest
		mbc    *MockBackendConfig
		expRes *FormatResponse
		expErr error
	}{
		"empty input": {
			req:    FormatRequest{},
			expErr: errors.New("empty DeviceList"),
		},
		"NVMe success": {
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{mockSingle.PciAddr},
			},
			mbc: &MockBackendConfig{
				FormatRes: &FormatResponse{
					DeviceResponses: DeviceFormatResponses{
						mockSingle.PciAddr: &DeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expRes: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					mockSingle.PciAddr: &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.mbc)

			gotRes, gotErr := p.Format(tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			common.AssertEqual(t, len(tc.expRes.DeviceResponses),
				len(gotRes.DeviceResponses), "number of device responses")
			for addr, resp := range tc.expRes.DeviceResponses {

				common.AssertEqual(t, resp, gotRes.DeviceResponses[addr],
					"device response")
			}
		})
	}
}
