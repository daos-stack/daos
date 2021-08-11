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
		Prepare(storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error)
		Reset(storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error)
		Scan(storage.BdevScanRequest) (*storage.BdevScanResponse, error)
		Format(storage.BdevFormatRequest) (*storage.BdevFormatResponse, error)
		// TODO DAOS-8040: re-enable VMD
		//		EnableVMD()
		//		IsVMDEnabled() bool
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

// TODO DAOS-8040: re-enable VMD
//func (p *Provider) enableVMD() {
//	p.backend.EnableVMD()
//}

// IsVMDEnabled returns true if provider has enabled VMD device awareness.
//func (p *Provider) IsVMDEnabled() bool {
//	return p.backend.IsVMDEnabled()
//}

// Scan calls into the backend to discover NVMe components in the
// system.
func (p *Provider) Scan(req storage.BdevScanRequest) (resp *storage.BdevScanResponse, err error) {
	// TODO DAOS-8040: re-enable VMD
	// set vmd state on remote provider in forwarded request
	//	if req.IsForwarded() && req.EnableVMD {
	//		p.enableVMD()
	//	}

	return p.backend.Scan(req)
}

// Prepare attempts to perform all actions necessary to make NVMe components
// available for use by DAOS. If Reset_ is set, rebind devices to kernel and
// reset allocation of hugepages, otherwise rebind devices to user-space
// driver compatible with SPDK and allocate hugeages.
func (p *Provider) Prepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	if req.Reset_ {
		p.log.Debug("run bdev storage provider prepare reset")
		return p.backend.Reset(req)
	}

	p.log.Debug("run bdev storage provider prepare setup")
	return p.backend.Prepare(req)
}

// Format attempts to initialize NVMe devices for use by DAOS.
// Note that this is a no-op for non-NVMe devices.
func (p *Provider) Format(req storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	if len(req.Properties.DeviceList) == 0 {
		return nil, errors.New("empty DeviceList in FormatRequest")
	}

	// TODO DAOS-8040: re-enable VMD
	// set vmd state on remote provider in forwarded request
	//	if req.IsForwarded() && req.EnableVMD {
	//		p.enableVMD()
	//	}

	return p.backend.Format(req)
}

// WriteNvmeConfig calls into the bdev backend to create an nvme config file.
func (p *Provider) WriteNvmeConfig(req storage.BdevWriteNvmeConfigRequest) (*storage.BdevWriteNvmeConfigResponse, error) {
	return p.backend.WriteNvmeConfig(req)
}
