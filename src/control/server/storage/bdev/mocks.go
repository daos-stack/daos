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
	"regexp"
	"strconv"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	MockBackendConfig struct {
		formatIdx       int
		PrepareResetErr error
		PrepareResp     *PrepareResponse
		PrepareErr      error
		FormatRes       *FormatResponse
		FormatFailIdx   int
		FormatErr       error
		ScanRes         *ScanResponse
		ScanErr         error
		vmdEnabled      bool // set through public access methods
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

func (mb *MockBackend) Scan(_ ScanRequest) (*ScanResponse, error) {
	if mb.cfg.ScanRes == nil {
		mb.cfg.ScanRes = new(ScanResponse)
	}
	return mb.cfg.ScanRes, mb.cfg.ScanErr
}

func (mb *MockBackend) Format(req FormatRequest) (*FormatResponse, error) {
	if mb.cfg.FormatRes != nil || mb.cfg.FormatErr != nil {
		if mb.cfg.FormatErr != nil && mb.cfg.FormatFailIdx == mb.cfg.formatIdx {
			mb.cfg.formatIdx++
			return nil, mb.cfg.FormatErr
		}
		mb.cfg.formatIdx++

		if mb.cfg.FormatRes != nil {
			return mb.cfg.FormatRes, nil
		}
	}

	if len(req.DeviceList) == 0 {
		return new(FormatResponse), nil
	}

	addr := req.DeviceList[0]
	mockRe := regexp.MustCompile(`^0000:80:00.(\d+)$`)
	if match := mockRe.FindStringSubmatch(addr); match != nil {
		idx, err := strconv.ParseInt(match[1], 16, 32)
		if err != nil {
			return nil, err
		}
		addr = storage.MockNvmeController(int32(idx)).PciAddr
	}

	return &FormatResponse{
		DeviceResponses: DeviceFormatResponses{
			addr: &DeviceFormatResponse{
				Formatted: true,
			},
		},
	}, nil
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

func (mb *MockBackend) EnableVmd() {
	mb.cfg.vmdEnabled = true
}

func (mb *MockBackend) IsVmdEnabled() bool {
	return mb.cfg.vmdEnabled
}

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig) *Provider {
	return NewProvider(log, NewMockBackend(mbc)).WithForwardingDisabled()
}

func DefaultMockProvider(log logging.Logger) *Provider {
	return NewMockProvider(log, nil).WithForwardingDisabled()
}
