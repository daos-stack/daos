//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"sync"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	MockBackendConfig struct {
		ResetErr    error
		PrepareResp *storage.BdevPrepareResponse
		PrepareErr  error
		FormatRes   *storage.BdevFormatResponse
		FormatErr   error
		ScanRes     *storage.BdevScanResponse
		ScanErr     error
		VmdEnabled  bool // set disabled by default
		UpdateErr   error
	}

	MockBackend struct {
		sync.RWMutex
		cfg          MockBackendConfig
		PrepareCalls []storage.BdevPrepareRequest
		ResetCalls   []storage.BdevPrepareRequest
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

	return mb.cfg.ScanRes, mb.cfg.ScanErr
}

func (mb *MockBackend) Format(req storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	if mb.cfg.FormatRes == nil {
		mb.cfg.FormatRes = new(storage.BdevFormatResponse)
	}

	return mb.cfg.FormatRes, mb.cfg.FormatErr
}

func (mb *MockBackend) Prepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	mb.Lock()
	if req.EnableVMD {
		mb.PrepareCalls = append(mb.PrepareCalls, req)
	}
	mb.PrepareCalls = append(mb.PrepareCalls, req)
	mb.Unlock()

	switch {
	case mb.cfg.PrepareErr != nil:
		return nil, mb.cfg.PrepareErr
	case mb.cfg.PrepareResp == nil:
		return new(storage.BdevPrepareResponse), nil
	default:
		return mb.cfg.PrepareResp, nil
	}
}

func (mb *MockBackend) Reset(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	mb.Lock()
	if req.EnableVMD {
		mb.ResetCalls = append(mb.ResetCalls, req)
	}
	mb.ResetCalls = append(mb.ResetCalls, req)
	mb.Unlock()

	if mb.cfg.ResetErr != nil {
		return nil, mb.cfg.ResetErr
	}

	return new(storage.BdevPrepareResponse), nil
}

func (mb *MockBackend) EnableVMD() {
	mb.cfg.VmdEnabled = true
}

func (mb *MockBackend) IsVMDEnabled() bool {
	return mb.cfg.VmdEnabled
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
