//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025 Google LLC
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
		VMDEnabled   bool // VMD is disabled by default
		PrepareRes   *storage.BdevPrepareResponse
		PrepareErr   error
		ResetRes     *storage.BdevPrepareResponse
		ResetErr     error
		ScanRes      *storage.BdevScanResponse
		ScanErr      error
		FormatRes    *storage.BdevFormatResponse
		FormatErr    error
		WriteConfRes *storage.BdevWriteConfigResponse
		WriteConfErr error
		UpdateErr    error
	}

	MockBackend struct {
		sync.RWMutex
		cfg            MockBackendConfig
		PrepareCalls   []storage.BdevPrepareRequest
		ResetCalls     []storage.BdevPrepareRequest
		WriteConfCalls []storage.BdevWriteConfigRequest
		ScanCalls      []storage.BdevScanRequest
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
	mb.Lock()
	mb.ScanCalls = append(mb.ScanCalls, req)
	mb.Unlock()

	switch {
	case mb.cfg.ScanErr != nil:
		return nil, mb.cfg.ScanErr
	case mb.cfg.ScanRes == nil:
		return &storage.BdevScanResponse{}, nil
	default:
		return mb.cfg.ScanRes, nil
	}
}

func (mb *MockBackend) Format(req storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	switch {
	case mb.cfg.FormatErr != nil:
		return nil, mb.cfg.FormatErr
	case mb.cfg.FormatRes == nil:
		return &storage.BdevFormatResponse{}, nil
	default:
		return mb.cfg.FormatRes, nil
	}
}

func (mb *MockBackend) Prepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	mb.Lock()
	mb.PrepareCalls = append(mb.PrepareCalls, req)
	mb.Unlock()

	switch {
	case mb.cfg.PrepareErr != nil:
		return nil, mb.cfg.PrepareErr
	case mb.cfg.PrepareRes == nil:
		return &storage.BdevPrepareResponse{}, nil
	default:
		return mb.cfg.PrepareRes, nil
	}
}

func (mb *MockBackend) Reset(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	mb.Lock()
	mb.ResetCalls = append(mb.ResetCalls, req)
	mb.Unlock()

	switch {
	case mb.cfg.ResetErr != nil:
		return nil, mb.cfg.ResetErr
	case mb.cfg.ResetRes == nil:
		return &storage.BdevPrepareResponse{}, nil
	default:
		return mb.cfg.ResetRes, nil
	}
}

func (mb *MockBackend) UpdateFirmware(_ string, _ string, _ int32) error {
	return mb.cfg.UpdateErr
}

func (mb *MockBackend) WriteConfig(req storage.BdevWriteConfigRequest) (*storage.BdevWriteConfigResponse, error) {
	mb.Lock()
	mb.WriteConfCalls = append(mb.WriteConfCalls, req)
	mb.Unlock()

	switch {
	case mb.cfg.WriteConfErr != nil:
		return nil, mb.cfg.WriteConfErr
	case mb.cfg.WriteConfRes == nil:
		return &storage.BdevWriteConfigResponse{}, nil
	default:
		return mb.cfg.WriteConfRes, nil
	}
}

func (mb *MockBackend) ReadConfig(_ storage.BdevReadConfigRequest) (*storage.BdevReadConfigResponse, error) {
	return &storage.BdevReadConfigResponse{}, nil
}

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig) *Provider {
	return NewProvider(log, NewMockBackend(mbc))
}

func DefaultMockProvider(log logging.Logger) *Provider {
	return NewMockProvider(log, nil)
}
