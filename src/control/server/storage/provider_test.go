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
		"bypass cache; scan error": {
			scanReq: BdevScanRequest{BypassCache: true},
			cache: &BdevScanResponse{
				Controllers: MockNvmeControllers(3),
			},
			scanResp: &BdevScanResponse{
				Controllers: MockNvmeControllers(2),
			},
			scanErr: errors.New("fail"),
			expErr:  errors.New("fail"),
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
		"ignore nil cache; no devices in scan": {
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
