//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	MockBackendConfig struct {
		PrepareResetErr error
		PrepareResp     *storage.BdevPrepareResponse
		PrepareErr      error
		FormatRes       *storage.BdevFormatResponse
		FormatErr       error
		ScanRes         *storage.BdevScanResponse
		ScanErr         error
		VmdEnabled      bool // set disabled by default
		UpdateErr       error
	}

	MockBackend struct {
		cfg MockBackendConfig
	}
)

func NewMockBackend(cfg *MockBackendConfig) *MockBackend {
	if cfg == nil {
		cfg = &MockBackendConfig{}
	}

	return &MockBackend{
		cfg: *cfg,
	}
}

func DefaultMockBackend() *MockBackend {
	return NewMockBackend(nil)
}

func (mb *MockBackend) Scan(req storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
	if mb.cfg.ScanRes == nil {
		mb.cfg.ScanRes = new(storage.BdevScanResponse)
	}
	// hack: filter based on request here because mock
	// provider has forwarding disabled and filter is
	// therefore skipped in test
	_, resp := filterScanResp(mb.cfg.ScanRes, req.DeviceList...)

	return resp, mb.cfg.ScanErr
}

func (mb *MockBackend) Format(req storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	if mb.cfg.FormatRes == nil {
		mb.cfg.FormatRes = new(storage.BdevFormatResponse)
	}

	return mb.cfg.FormatRes, mb.cfg.FormatErr
}

func (mb *MockBackend) PrepareReset() error {
	return mb.cfg.PrepareResetErr
}

func (mb *MockBackend) Prepare(_ storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	if mb.cfg.PrepareErr != nil {
		return nil, mb.cfg.PrepareErr
	}
	if mb.cfg.PrepareResp == nil {
		return new(storage.BdevPrepareResponse), nil
	}

	return mb.cfg.PrepareResp, nil
}

func (mb *MockBackend) DisableVMD() {
	mb.cfg.VmdEnabled = false
}

func (mb *MockBackend) IsVMDDisabled() bool {
	return !mb.cfg.VmdEnabled
}

func (mb *MockBackend) UpdateFirmware(_ string, _ string, _ int32) error {
	return mb.cfg.UpdateErr
}

func (mb *MockBackend) WriteNvmeConfig(req storage.BdevWriteNvmeConfigRequest) (*storage.BdevWriteNvmeConfigResponse, error) {
	return &storage.BdevWriteNvmeConfigResponse{}, nil
}

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig) *Provider {
	return NewProvider(log, NewMockBackend(mbc))
}

func DefaultMockProvider(log logging.Logger) *Provider {
	return NewMockProvider(log, nil)
}
