//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

// defBdevCmpOpts returns a default set of cmp option suitable for this package
func defBdevCmpOpts() []cmp.Option {
	return []cmp.Option{
		// ignore these fields on most tests, as they are intentionally not stable
		cmpopts.IgnoreFields(NvmeController{}, "HealthStats", "Serial"),
	}
}

func Test_filterScanResp(t *testing.T) {
	ctrlr1 := MockNvmeController(1)
	ctrlr2 := MockNvmeController(2)
	ctrlr3 := MockNvmeController(3)
	ctrlr4 := MockNvmeController(4)
	ctrlr5 := MockNvmeController(5)

	for name, tc := range map[string]struct {
		scanResp   *BdevScanResponse
		deviceList []string
		expResp    *BdevScanResponse
		expErr     error
	}{
		"nil scan response": {
			expErr: errors.New("nil response"),
		},
		"scan response no filter": {
			scanResp: &BdevScanResponse{
				Controllers: NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
			expResp: &BdevScanResponse{
				Controllers: NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
		},
		"scan response filtered": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr3.PciAddr},
			scanResp: &BdevScanResponse{
				Controllers: NvmeControllers{ctrlr1, ctrlr2, ctrlr3},
			},
			expResp: &BdevScanResponse{
				Controllers: NvmeControllers{ctrlr1, ctrlr3},
			},
		},
		"scan response inclusive filter": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr2.PciAddr, ctrlr3.PciAddr},
			scanResp: &BdevScanResponse{
				Controllers: NvmeControllers{ctrlr1, ctrlr3},
			},
			expResp: &BdevScanResponse{
				Controllers: NvmeControllers{ctrlr1, ctrlr3},
			},
		},
		"scan response exclusive filter": {
			deviceList: []string{ctrlr1.PciAddr, ctrlr5.PciAddr, ctrlr3.PciAddr},
			scanResp: &BdevScanResponse{
				Controllers: NvmeControllers{ctrlr2, ctrlr4},
			},
			expResp: &BdevScanResponse{
				Controllers: NvmeControllers{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			gotResp, gotErr := filterScanResp(log, tc.scanResp, tc.deviceList...)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defBdevCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func Test_scanBdevs(t *testing.T) {
	for name, tc := range map[string]struct {
		scanReq  BdevScanRequest
		cache    *BdevScanResponse
		scanResp *BdevScanResponse
		scanErr  error
		expMsg   string
		expResp  *BdevScanResponse
		expErr   error
	}{
		"scan error": {
			scanReq: BdevScanRequest{},
			scanErr: errors.New("fail"),
			expErr:  errors.New("fail"),
		},
		"nil scan response": {
			scanReq:  BdevScanRequest{},
			scanResp: nil,
			expErr:   errors.New("nil response"),
		},
		"nil devices": {
			scanReq:  BdevScanRequest{},
			scanResp: new(BdevScanResponse),
			expResp: &BdevScanResponse{
				Controllers: NvmeControllers{},
			},
		},
		"no devices": {
			scanReq: BdevScanRequest{},
			scanResp: &BdevScanResponse{
				Controllers: NvmeControllers{},
			},
			expResp: &BdevScanResponse{
				Controllers: NvmeControllers{},
			},
		},
		"use cache": {
			scanReq: BdevScanRequest{},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
			expResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
		},
		"bypass cache": {
			scanReq: BdevScanRequest{BypassCache: true},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
			expResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
		},
		"ignore nil cache": {
			scanReq: BdevScanRequest{},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			expResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
		},
		"ignore empty cache": {
			scanReq: BdevScanRequest{},
			cache:   &BdevScanResponse{},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			expResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
		},
		"filtered devices from backend scan": {
			scanReq: BdevScanRequest{
				DeviceList: []string{
					MockNvmeController(0).PciAddr,
					MockNvmeController(1).PciAddr,
				},
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			expResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
		},
		"filtered devices from cache": {
			scanReq: BdevScanRequest{
				DeviceList: []string{
					MockNvmeController(0).PciAddr,
					MockNvmeController(1).PciAddr,
				},
			},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			expResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			scanFn := func(r BdevScanRequest) (*BdevScanResponse, error) {
				return tc.scanResp, tc.scanErr
			}

			gotResp, gotErr := scanBdevs(log, tc.scanReq, tc.cache, scanFn)
			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defBdevCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
