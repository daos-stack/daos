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

func TestBdevScan(t *testing.T) {
	for name, tc := range map[string]struct {
		req            ScanRequest
		forwarded      bool
		mbc            *MockBackendConfig
		expRes         *ScanResponse
		expErr         error
		expVMDDisabled bool
	}{
		"no devices": {
			req:    ScanRequest{},
			expRes: &ScanResponse{},
		},
		"single device": {
			req: ScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: &ScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(),
					},
				},
			},
			expRes: &ScanResponse{
				Controllers: storage.NvmeControllers{storage.MockNvmeController()},
			},
		},
		"multiple devices": {
			req: ScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: &ScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
						storage.MockNvmeController(2),
						storage.MockNvmeController(3),
					},
				},
			},
			expRes: &ScanResponse{
				Controllers: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
				},
			},
		},
		"multiple devices with vmd disabled": {
			req:       ScanRequest{DisableVMD: true},
			forwarded: true,
			mbc: &MockBackendConfig{
				ScanRes: &ScanResponse{
					Controllers: storage.NvmeControllers{
						storage.MockNvmeController(1),
						storage.MockNvmeController(2),
						storage.MockNvmeController(3),
					},
				},
			},
			expRes: &ScanResponse{
				Controllers: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
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
