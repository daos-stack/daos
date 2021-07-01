//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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

func TestProvider_ScanResponse_filter(t *testing.T) {
	ctrlr1 := storage.MockNvmeController(1)
	ctrlr2 := storage.MockNvmeController(2)
	ctrlr3 := storage.MockNvmeController(3)
	ctrlr4 := storage.MockNvmeController(4)
	ctrlr5 := storage.MockNvmeController(5)

	for name, tc := range map[string]struct {
		scanResp   *storage.BdevScanResponse
		deviceList []string
		expResp    *storage.BdevScanResponse
		expNum     int
	}{
		"scan response no filter": {
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
			expResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
		},
		"scan response filtered": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr3.PciAddr},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
			expResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr3},
			},
			expNum: 1,
		},
		"scan response inclusive filter": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr2.PciAddr, ctrlr3.PciAddr},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr3},
			},
			expResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{ctrlr1, ctrlr3},
			},
		},
		"scan response exclusive filter": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr5.PciAddr, ctrlr3.PciAddr},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{ctrlr2, ctrlr4},
			},
			expResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{},
			},
			expNum: 2,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotNum, gotResp := filterScanResp(tc.scanResp, tc.deviceList...)

			common.AssertEqual(t, tc.expNum, gotNum, name+" expected number filtered")
			if diff := cmp.Diff(tc.expResp, gotResp, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBdev_forwardScan(t *testing.T) {
	for name, tc := range map[string]struct {
		scanReq      storage.BdevScanRequest
		cache        *storage.BdevScanResponse
		scanResp     *storage.BdevScanResponse
		scanErr      error
		shouldUpdate bool
		expMsg       string
		expResp      *storage.BdevScanResponse
		expErr       error
	}{
		"scan error": {
			scanReq: storage.BdevScanRequest{},
			scanErr: errors.New("fail"),
			expErr:  errors.New("fail"),
		},
		"nil scan response": {
			scanReq:  storage.BdevScanRequest{},
			scanResp: nil,
			expErr:   errors.New("unexpected nil response from bdev backend"),
		},
		"nil devices": {
			scanReq:      storage.BdevScanRequest{},
			scanResp:     new(storage.BdevScanResponse),
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (0 devices)",
			expResp:      new(storage.BdevScanResponse),
		},
		"no devices": {
			scanReq: storage.BdevScanRequest{},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{},
			},
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (0 devices)",
			expResp: &storage.BdevScanResponse{
				Controllers: storage.NvmeControllers{},
			},
		},
		"update cache": {
			scanReq: storage.BdevScanRequest{},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (3 devices)",
			expResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
		},
		"update empty cache": {
			scanReq: storage.BdevScanRequest{},
			cache:   &storage.BdevScanResponse{},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (3 devices)",
			expResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
		},
		"reuse cache": {
			scanReq: storage.BdevScanRequest{},
			cache: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(2),
			},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			expMsg: "bdev scan: reuse cache (2 devices)",
			expResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(2),
			},
		},
		"bypass cache": {
			scanReq: storage.BdevScanRequest{NoCache: true},
			cache: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(2),
			},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			expMsg: "bdev scan: bypass cache (3 devices)",
			expResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
		},
		"filtered devices": {
			scanReq: storage.BdevScanRequest{
				DeviceList: []string{
					storage.MockNvmeController(0).PciAddr,
					storage.MockNvmeController(1).PciAddr,
				},
			},
			scanResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(3),
			},
			shouldUpdate: true,
			expMsg:       "bdev scan: update cache (3-1 filtered devices)",
			expResp: &storage.BdevScanResponse{
				Controllers: storage.MockNvmeControllers(2),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			scanFn := func(r storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
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

func TestProvider_Scan(t *testing.T) {
	ctrlr1 := storage.MockNvmeController(1)
	ctrlr2 := storage.MockNvmeController(2)
	ctrlr3 := storage.MockNvmeController(3)

	for name, tc := range map[string]struct {
		req            storage.BdevScanRequest
		forwarded      bool
		mbc            *MockBackendConfig
		expRes         *storage.BdevScanResponse
		expErr         error
		expVMDDisabled bool
	}{
		"no devices": {
			req:            storage.BdevScanRequest{},
			expRes:         &storage.BdevScanResponse{},
			expVMDDisabled: true, // disabled in mock by default
		},
		"single device": {
			req: storage.BdevScanRequest{},
			mbc: &MockBackendConfig{
				ScanRes: &storage.BdevScanResponse{
					Controllers: storage.NvmeControllers{ctrlr1},
				},
				VmdEnabled: true,
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
			expVMDDisabled: true,
		},
		"multiple devices with vmd disabled": {
			req:       storage.BdevScanRequest{DisableVMD: true},
			forwarded: true,
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
			expVMDDisabled: true,
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
			req: storage.BdevPrepareRequest{},
			mbc: &MockBackendConfig{
				PrepareResetErr: errors.New("reset failed"),
			},
			expErr: errors.New("reset failed"),
		},
		"reset-only": {
			req: storage.BdevPrepareRequest{
				ResetOnly: true,
			},
			mbc: &MockBackendConfig{
				PrepareErr: errors.New("should not get this far"),
			},
			expRes: &storage.BdevPrepareResponse{},
		},
		"prepare fails": {
			req: storage.BdevPrepareRequest{},
			mbc: &MockBackendConfig{
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
					DeviceList: []string{mockSingle.PciAddr},
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
					DeviceList: []string{mockSingle.PciAddr},
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
