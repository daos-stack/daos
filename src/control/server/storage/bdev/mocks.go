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
		formatIdx     int
		InitErr       error
		ResetErr      error
		PrepareErr    error
		FormatRes     *storage.NvmeController
		FormatFailIdx int
		FormatErr     error
		ScanRes       storage.NvmeControllers
		ScanErr       error
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

func (mb *MockBackend) Init(_ ...int) error {
	return mb.cfg.InitErr
}

func (mb *MockBackend) Scan() (storage.NvmeControllers, error) {
	if err := mb.Init(); err != nil {
		return nil, err
	}

	return mb.cfg.ScanRes, mb.cfg.ScanErr
}

func (mb *MockBackend) Format(pciAddr string) (*storage.NvmeController, error) {
	if err := mb.Init(); err != nil {
		return nil, err
	}

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

	mockRe := regexp.MustCompile(`^0000:80:00.(\d+)$`)
	if match := mockRe.FindStringSubmatch(pciAddr); match != nil {
		idx, err := strconv.ParseInt(match[1], 16, 32)
		if err != nil {
			return nil, err
		}
		return storage.MockNvmeController(int32(idx)), nil
	}

	return &storage.NvmeController{
		PciAddr: pciAddr,
	}, nil
}

func (mb *MockBackend) Reset() error {
	return mb.cfg.ResetErr
}

func (mb *MockBackend) Prepare(_ PrepareRequest) error {
	return mb.cfg.PrepareErr
}

func (mb *MockBackend) EnableVmd() error {
	return nil
}

func (mb *MockBackend) IsVmdEnabled() bool {
	return false
}

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig) *Provider {
	return NewProvider(log, NewMockBackend(mbc)).WithForwardingDisabled()
}

func DefaultMockProvider(log logging.Logger) *Provider {
	return NewMockProvider(log, nil).WithForwardingDisabled()
}
