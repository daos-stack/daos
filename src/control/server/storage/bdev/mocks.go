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
