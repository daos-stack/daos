//
// (C) Copyright 2019 Intel Corporation.
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
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	// InitRequest defines the parameters for initializing the provider.
	InitRequest struct {
		SPDKShmID int
	}

	// ScanRequest defines the parameters for a Scan operation.
	ScanRequest struct {
		Forwarded bool
	}

	// ScanResponse contains information gleaned during a successful Scan operation.
	ScanResponse struct {
		Controllers storage.NvmeControllers
	}

	// PrepareRequest defines the parameters for a Prepare operation.
	PrepareRequest struct {
		Forwarded     bool
		HugePageCount int
		PCIWhitelist  string
		TargetUser    string
		ResetOnly     bool
	}

	// PrepareResponse contains the results of a successful Prepare operation.
	PrepareResponse struct{}

	// FormatRequest defines the parameters for a Format operation.
	FormatRequest struct {
		Forwarded  bool
		Class      storage.BdevClass
		DeviceList []string
	}

	// DeviceFormatResponse contains device-specific Format operation results.
	DeviceFormatResponse struct {
		Formatted  bool
		Error      error
		Controller *storage.NvmeController
	}

	// DeviceFormatResponses is a map of device identifiers to device Format results.
	DeviceFormatResponses map[string]*DeviceFormatResponse

	// FormatResults contains the results of a Format operation.
	FormatResponse struct {
		DeviceResponses DeviceFormatResponses
	}

	// Backend defines a set of methods to be implemented by a Block Device backend.
	Backend interface {
		Init(shmID ...int) error
		Reset() error
		Prepare(hugePageCount int, targetUser string, pciWhitelist string) error
		Scan() (storage.NvmeControllers, error)
		Format(pciAddr string) (*storage.NvmeController, error)
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
	return &Provider{
		log:     log,
		backend: backend,
	}
}

// Init performs any initialization steps required by the provider.
func (p *Provider) Init(req InitRequest) error {
	return p.backend.Init(req.SPDKShmID)
}

// Scan attempts to perform a scan to discover NVMe components in the system.
func (p *Provider) Scan(req ScanRequest) (*ScanResponse, error) {
	cs, err := p.backend.Scan()
	if err != nil {
		return nil, err
	}

	return &ScanResponse{
		Controllers: cs,
	}, nil
}

// Prepare attempts to perform all actions necessary to make NVMe components available for
// use by DAOS.
func (p *Provider) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	// run reset first to ensure reallocation of hugepages
	if err := p.backend.Reset(); err != nil {
		return nil, errors.WithMessage(err, "SPDK setup reset")
	}

	res := &PrepareResponse{}
	// if we're only resetting, just return before prep
	if req.ResetOnly {
		return res, nil
	}

	return res, errors.WithMessage(
		p.backend.Prepare(req.HugePageCount, req.TargetUser, req.PCIWhitelist),
		"SPDK setup",
	)
}

// Format attempts to initialize NVMe devices for use by DAOS (NB: no-op for non-NVMe devices).
func (p *Provider) Format(req FormatRequest) (*FormatResponse, error) {
	if len(req.DeviceList) == 0 {
		return nil, errors.New("empty DeviceList in FormatRequest")
	}

	// TODO: Kick off device formats in goroutines? Serially formatting a large
	// number of NVMe devices can be slow.
	res := &FormatResponse{
		DeviceResponses: make(DeviceFormatResponses),
	}

	for _, dev := range req.DeviceList {
		res.DeviceResponses[dev] = &DeviceFormatResponse{}
		switch req.Class {
		default:
			res.DeviceResponses[dev].Error = errors.Wrap(FaultFormatUnknownClass, req.Class.String())
		case storage.BdevClassKdev, storage.BdevClassFile, storage.BdevClassMalloc:
			res.DeviceResponses[dev].Formatted = true
			p.log.Infof("%s format for non-NVMe bdev skipped (%s)", req.Class, dev)
		case storage.BdevClassNvme:
			p.log.Infof("%s format starting (%s)", req.Class, dev)
			c, err := p.backend.Format(dev)
			if err != nil {
				p.log.Errorf("%s format failed (%s)", req.Class, dev)
				res.DeviceResponses[dev].Error = err
				continue
			}
			res.DeviceResponses[dev].Controller = c
			res.DeviceResponses[dev].Formatted = true
			p.log.Infof("%s format successful (%s)", req.Class, dev)
		}
	}

	return res, nil
}
