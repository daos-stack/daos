//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var _ storage.BdevProvider = &Provider{}

type (
	// Backend defines a set of methods to be implemented by a Block Device backend.
	Backend interface {
		PrepareReset() error
		Prepare(storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error)
		Scan(storage.BdevScanRequest) (*storage.BdevScanResponse, error)
		Format(storage.BdevFormatRequest) (*storage.BdevFormatResponse, error)
		//		DisableVMD()
		//		IsVMDDisabled() bool
		UpdateFirmware(pciAddr string, path string, slot int32) error
		WriteNvmeConfig(storage.BdevWriteNvmeConfigRequest) (*storage.BdevWriteNvmeConfigResponse, error)
	}

	// Provider encapsulates configuration and logic for interacting with a Block
	// Device Backend.
	Provider struct {
		log     logging.Logger
		backend Backend
	}
)

// DefaultProvider returns an initialized *Provider suitable for use in production code.
func DefaultProvider(log logging.Logger) *Provider {
	return NewProvider(log, defaultBackend(log))
}

// NewProvider returns an initialized *Provider.
func NewProvider(log logging.Logger, backend Backend) *Provider {
	p := &Provider{
		log:     log,
		backend: backend,
	}
	return p
}

//func (p *Provider) disableVMD() {
//	p.backend.DisableVMD()
//}

// IsVMDDisabled returns true if provider has disabled VMD device awareness.
//func (p *Provider) IsVMDDisabled() bool {
//	return p.backend.IsVMDDisabled()
//}

// Scan calls into the backend to discover NVMe components in the
// system.
func (p *Provider) Scan(req storage.BdevScanRequest) (resp *storage.BdevScanResponse, err error) {
	// set vmd state on remote provider in forwarded request
	//	if req.IsForwarded() && req.DisableVMD {
	//		p.disableVMD()
	//	}

	return p.backend.Scan(req)
}

// Prepare attempts to perform all actions necessary to make NVMe
// components available for use by DAOS.
func (p *Provider) Prepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	// run reset first to ensure reallocation of hugepages
	if err := p.backend.PrepareReset(); err != nil {
		return nil, errors.Wrap(err, "bdev prepare reset")
	}

	resp := new(storage.BdevPrepareResponse)
	// if we're only resetting, return before prep
	if req.ResetOnly {
		return resp, nil
	}

	return p.backend.Prepare(req)
}

// Format attempts to initialize NVMe devices for use by DAOS.
// Note that this is a no-op for non-NVMe devices.
func (p *Provider) Format(req storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	if len(req.Properties.DeviceList) == 0 {
		return nil, errors.New("empty DeviceList in FormatRequest")
	}

	// set vmd state on remote provider in forwarded request
	//	if req.IsForwarded() && req.DisableVMD {
	//		p.disableVMD()
	//	}

	return p.backend.Format(req)
}

func (p *Provider) WriteNvmeConfig(req storage.BdevWriteNvmeConfigRequest) (*storage.BdevWriteNvmeConfigResponse, error) {
	return p.backend.WriteNvmeConfig(req)
}
