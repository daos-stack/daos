//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestProvider_Scan(t *testing.T) {
	ctrlr1 := storage.MockNvmeController(1)
	ctrlr2 := storage.MockNvmeController(2)
	ctrlr3 := storage.MockNvmeController(3)

	for name, tc := range map[string]struct {
		req       storage.BdevScanRequest
		forwarded bool
		mbc       *MockBackendConfig
		expRes    *storage.BdevScanResponse
		expErr    error
	}{
		"no devices": {
			req:    storage.BdevScanRequest{},
			expRes: &storage.BdevScanResponse{},
		},
		"single device": {
			req: storage.BdevScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr1},
				},
			},
			expRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1},
			},
		},
		"multiple devices": {
			req: storage.BdevScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{
						ctrlr1, ctrlr2, ctrlr3,
					},
				},
			},
			expRes: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{
					ctrlr1, ctrlr2, ctrlr3,
				},
			},
		},
		"failure": {
			req: storage.BdevScanRequest{},
			mbc: &MockBackendConfig{
				ScanErr: errors.New("scan failed"),
			},
			expErr: errors.New("scan failed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.mbc)

			tc.req.Forwarded = tc.forwarded

			gotRes, gotErr := p.Scan(tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expRes, gotRes, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProvider_Prepare(t *testing.T) {
	for name, tc := range map[string]struct {
		req           storage.BdevPrepareRequest
		shouldForward bool
		mbc           *MockBackendConfig
		vmdDetectErr  error
		expRes        *storage.BdevPrepareResponse
		expErr        error
	}{
		"reset fails": {
			req: storage.BdevPrepareRequest{Reset_: true},
			mbc: &MockBackendConfig{
				ResetErr:   errors.New("reset failed"),
				PrepareErr: errors.New("prepare failed"),
			},
			expErr: errors.New("reset failed"),
		},
		"reset succeeds": {
			req:    storage.BdevPrepareRequest{Reset_: true},
			expRes: &storage.BdevPrepareResponse{},
		},
		"prepare fails": {
			req: storage.BdevPrepareRequest{},
			mbc: &MockBackendConfig{
				ResetErr:   errors.New("reset failed"),
				PrepareErr: errors.New("prepare failed"),
			},
			expErr: errors.New("prepare failed"),
		},
		"prepare succeeds": {
			req:    storage.BdevPrepareRequest{},
			expRes: &storage.BdevPrepareResponse{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.mbc)

			gotRes, gotErr := p.Prepare(tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expRes, gotRes); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestProvider_Format(t *testing.T) {
	mockSingle := storage.MockNvmeController()

	for name, tc := range map[string]struct {
		req    storage.BdevFormatRequest
		mbc    *MockBackendConfig
		expRes *storage.BdevFormatResponse
		expErr error
	}{
		"empty input": {
			req:    storage.BdevFormatRequest{},
			expErr: errors.New("empty DeviceList"),
		},
		"NVMe failure": {
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: storage.MustNewBdevDeviceList(mockSingle.PciAddr),
				},
			},
			mbc: &MockBackendConfig{
				FormatRes: &storage.BdevFormatResponse{
					DeviceResponses: storage.BdevDeviceFormatResponses{
						mockSingle.PciAddr: &storage.BdevDeviceFormatResponse{
							Error: FaultFormatError(mockSingle.PciAddr,
								errors.New("foobared")),
						},
					},
				},
			},
			expRes: &storage.BdevFormatResponse{
				DeviceResponses: storage.BdevDeviceFormatResponses{
					mockSingle.PciAddr: &storage.BdevDeviceFormatResponse{
						Error: FaultFormatError(mockSingle.PciAddr,
							errors.New("foobared")),
					},
				},
			},
		},
		"NVMe success": {
			req: storage.BdevFormatRequest{
				Properties: storage.BdevTierProperties{
					Class:      storage.ClassNvme,
					DeviceList: storage.MustNewBdevDeviceList(mockSingle.PciAddr),
				},
			},
			mbc: &MockBackendConfig{
				FormatRes: &storage.BdevFormatResponse{
					DeviceResponses: storage.BdevDeviceFormatResponses{
						mockSingle.PciAddr: &storage.BdevDeviceFormatResponse{
							Formatted: true,
						},
					},
				},
			},
			expRes: &storage.BdevFormatResponse{
				DeviceResponses: storage.BdevDeviceFormatResponses{
					mockSingle.PciAddr: &storage.BdevDeviceFormatResponse{
						Formatted: true,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			p := NewMockProvider(log, tc.mbc)

			gotRes, gotErr := p.Format(tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			test.AssertEqual(t, len(tc.expRes.DeviceResponses),
				len(gotRes.DeviceResponses), "number of device responses")
			for addr, resp := range tc.expRes.DeviceResponses {

				test.AssertEqual(t, resp, gotRes.DeviceResponses[addr],
					"device response")
			}
		})
	}
}
