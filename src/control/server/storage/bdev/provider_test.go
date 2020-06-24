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
		req    ScanRequest
		mbc    *MockBackendConfig
		expRes *ScanResponse
		expErr error
	}{
		"no devices": {
			req:    ScanRequest{},
			expRes: &ScanResponse{},
		},
		"single device": {
			req: ScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: storage.NvmeControllers{storage.MockNvmeController()},
			},
			expRes: &ScanResponse{
				Controllers: storage.NvmeControllers{storage.MockNvmeController()},
			},
		},
		"multiple devices": {
			req: ScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
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

			gotRes, gotErr := p.Scan(tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expRes, gotRes, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBdevPrepare(t *testing.T) {
	for name, tc := range map[string]struct {
		req    PrepareRequest
		mbc    *MockBackendConfig
		expRes *PrepareResponse
		expErr error
	}{
		"reset fails": {
			req: PrepareRequest{},
			mbc: &MockBackendConfig{
				ResetErr: errors.New("reset failed"),
			},
			expErr: errors.New("reset failed"),
		},
		"reset-only": {
			req: PrepareRequest{
				ResetOnly: true,
			},
			mbc: &MockBackendConfig{
				PrepareErr: errors.New("we shouldnt get this far"),
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
		"unknown device class": {
			req: FormatRequest{
				Class:      storage.BdevClass("whoops"),
				DeviceList: []string{"foo"},
			},
			expRes: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					"foo": &DeviceFormatResponse{
						Error: FaultFormatUnknownClass("whoops"),
					},
				},
			},
		},
		"kdev": {
			req: FormatRequest{
				Class:      storage.BdevClassKdev,
				DeviceList: []string{"foo"},
			},
			expRes: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					"foo": &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
		"malloc": {
			req: FormatRequest{
				Class:      storage.BdevClassMalloc,
				DeviceList: []string{"foo"},
			},
			expRes: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					"foo": &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
		"file": {
			req: FormatRequest{
				Class:      storage.BdevClassFile,
				DeviceList: []string{"foo"},
			},
			expRes: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					"foo": &DeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
		"NVMe single success": {
			req: FormatRequest{
				Class:      storage.BdevClassNvme,
				DeviceList: []string{mockSingle.PciAddr},
			},
			expRes: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					mockSingle.PciAddr: &DeviceFormatResponse{
						Formatted:  true,
						Controller: mockSingle,
					},
				},
			},
		},
		"NVMe triple success": {
			req: FormatRequest{
				Class: storage.BdevClassNvme,
				DeviceList: []string{
					mockSingle.PciAddr,
					storage.MockNvmeController(2).PciAddr,
					storage.MockNvmeController(3).PciAddr,
				},
			},
			expRes: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					mockSingle.PciAddr: &DeviceFormatResponse{
						Formatted:  true,
						Controller: mockSingle,
					},
					storage.MockNvmeController(2).PciAddr: &DeviceFormatResponse{
						Formatted:  true,
						Controller: storage.MockNvmeController(2),
					},
					storage.MockNvmeController(3).PciAddr: &DeviceFormatResponse{
						Formatted:  true,
						Controller: storage.MockNvmeController(3),
					},
				},
			},
		},
		"NVMe two success, one failure": {
			mbc: &MockBackendConfig{
				FormatFailIdx: 1,
				FormatErr:     errors.New("format failed"),
			},
			req: FormatRequest{
				Class: storage.BdevClassNvme,
				DeviceList: []string{
					mockSingle.PciAddr,
					storage.MockNvmeController(2).PciAddr,
					storage.MockNvmeController(3).PciAddr,
				},
			},
			expRes: &FormatResponse{
				DeviceResponses: DeviceFormatResponses{
					mockSingle.PciAddr: &DeviceFormatResponse{
						Formatted:  true,
						Controller: mockSingle,
					},
					storage.MockNvmeController(2).PciAddr: &DeviceFormatResponse{
						Formatted: false,
						Error: FaultFormatError(
							storage.MockNvmeController(2).PciAddr,
							errors.New("format failed")),
					},
					storage.MockNvmeController(3).PciAddr: &DeviceFormatResponse{
						Formatted:  true,
						Controller: storage.MockNvmeController(3),
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

			cmpOpts := []cmp.Option{
				cmp.Comparer(common.CmpErrBool),
			}
			cmpOpts = append(cmpOpts, defCmpOpts()...)
			if diff := cmp.Diff(tc.expRes, gotRes, cmpOpts...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
