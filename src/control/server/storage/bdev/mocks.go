//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	MockBackendConfig struct {
		PrepareResetErr error
		PrepareResp     *PrepareResponse
		PrepareErr      error
		FormatRes       *FormatResponse
		FormatErr       error
		ScanRes         *ScanResponse
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

func (mb *MockBackend) Scan(req ScanRequest) (*ScanResponse, error) {
	if mb.cfg.ScanRes == nil {
		mb.cfg.ScanRes = new(ScanResponse)
	}
	// hack: filter based on request here because mock
	// provider has forwarding disabled and filter is
	// therefore skipped in test
	_, resp := mb.cfg.ScanRes.filter(req.DeviceList...)

	return resp, mb.cfg.ScanErr
}

func (mb *MockBackend) Format(req FormatRequest) (*FormatResponse, error) {
	if mb.cfg.FormatRes == nil {
		mb.cfg.FormatRes = new(FormatResponse)
	}

	return mb.cfg.FormatRes, mb.cfg.FormatErr
}

func (mb *MockBackend) PrepareReset() error {
	return mb.cfg.PrepareResetErr
}

func (mb *MockBackend) Prepare(_ PrepareRequest) (*PrepareResponse, error) {
	if mb.cfg.PrepareErr != nil {
		return nil, mb.cfg.PrepareErr
	}
	if mb.cfg.PrepareResp == nil {
		return new(PrepareResponse), nil
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

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig) *Provider {
	return NewProvider(log, NewMockBackend(mbc)).WithForwardingDisabled()
}

func DefaultMockProvider(log logging.Logger) *Provider {
	return NewMockProvider(log, nil).WithForwardingDisabled()
}
