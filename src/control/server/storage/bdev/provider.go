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
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	// ScanRequest defines the parameters for a Scan operation.
	ScanRequest struct {
		pbin.ForwardableRequest
		DeviceList []string
		DisableVMD bool
		Rescan     bool
	}

	// ScanResponse contains information gleaned during a successful Scan operation.
	ScanResponse struct {
		Controllers storage.NvmeControllers
	}

	// PrepareRequest defines the parameters for a Prepare operation.
	PrepareRequest struct {
		pbin.ForwardableRequest
		HugePageCount int
		PCIWhitelist  string
		PCIBlacklist  string
		TargetUser    string
		ResetOnly     bool
		DisableVFIO   bool
		DisableVMD    bool
	}

	// PrepareResponse contains the results of a successful Prepare operation.
	PrepareResponse struct {
		VmdDetected bool
	}

	// FormatRequest defines the parameters for a Format operation.
	FormatRequest struct {
		pbin.ForwardableRequest
		Class      storage.BdevClass
		DeviceList []string
		MemSize    int // size MiB memory to be used by SPDK proc
		DisableVMD bool
	}

	// DeviceFormatRequest designs the parameters for a device-specific format.
	DeviceFormatRequest struct {
		MemSize int // size MiB memory to be used by SPDK proc
		Device  string
		Class   storage.BdevClass
	}

	// DeviceFormatResponse contains device-specific Format operation results.
	DeviceFormatResponse struct {
		Formatted bool
		Error     *fault.Fault
	}

	// DeviceFormatResponses is a map of device identifiers to device Format results.
	DeviceFormatResponses map[string]*DeviceFormatResponse

	// FormatResponse contains the results of a Format operation.
	FormatResponse struct {
		DeviceResponses DeviceFormatResponses
	}

	// Backend defines a set of methods to be implemented by a Block Device backend.
	Backend interface {
		PrepareReset() error
		Prepare(PrepareRequest) (*PrepareResponse, error)
		Scan(ScanRequest) (*ScanResponse, error)
		Format(FormatRequest) (*FormatResponse, error)
		DisableVMD()
		IsVMDDisabled() bool
		UpdateFirmware(pciAddr string, path string, slot int32) error
	}

	// Provider encapsulates configuration and logic for interacting with a Block
	// Device Backend.
	Provider struct {
		sync.Mutex // ensure mutually exclusive access to scan cache
		firmwareProvider
		log       logging.Logger
		backend   Backend
		fwd       *Forwarder
		scanCache *ScanResponse
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
		fwd:     NewForwarder(log),
	}
	p.setupFirmwareProvider(log)
	return p
}

// WithForwardingDisabled returns a provider with forwarding disabled.
func (p *Provider) WithForwardingDisabled() *Provider {
	p.fwd.Disabled = true
	return p
}

func (p *Provider) shouldForward(req pbin.ForwardChecker) bool {
	return !p.fwd.Disabled && !req.IsForwarded()
}

func (p *Provider) disableVMD() {
	p.backend.DisableVMD()
}

// IsVMDDisabled returns true if provider has disabled VMD device awareness.
func (p *Provider) IsVMDDisabled() bool {
	return p.backend.IsVMDDisabled()
}

// Scan attempts to perform a scan to discover NVMe components in the system.
func (p *Provider) Scan(req ScanRequest) (*ScanResponse, error) {
	if p.shouldForward(req) {
		req.DisableVMD = p.IsVMDDisabled()

		p.Lock()
		defer p.Unlock()

		if p.scanCache == nil || req.Rescan {
			p.log.Debug("bdev provider rescan requested")

			resp, err := p.fwd.Scan(req)
			if err != nil {
				return nil, err
			}
			p.scanCache = resp
		}

		return p.scanCache, nil
	}
	// set vmd state on remote provider in forwarded request
	if req.IsForwarded() && req.DisableVMD {
		p.disableVMD()
	}

	return p.backend.Scan(req)
}

// Prepare attempts to perform all actions necessary to make NVMe components available for
// use by DAOS.
func (p *Provider) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	if p.shouldForward(req) {
		resp, err := p.fwd.Prepare(req)
		// set vmd state on local provider after forwarding request
		if err == nil && !resp.VmdDetected {
			p.disableVMD()
		}

		return resp, err
	}

	// run reset first to ensure reallocation of hugepages
	if err := p.backend.PrepareReset(); err != nil {
		return nil, errors.Wrap(err, "bdev prepare reset")
	}

	resp := new(PrepareResponse)
	// if we're only resetting, return before prep
	if req.ResetOnly {
		return resp, nil
	}

	return p.backend.Prepare(req)
}

// Format attempts to initialize NVMe devices for use by DAOS (NB: no-op for non-NVMe devices).
func (p *Provider) Format(req FormatRequest) (*FormatResponse, error) {
	if len(req.DeviceList) == 0 {
		return nil, errors.New("empty DeviceList in FormatRequest")
	}

	if p.shouldForward(req) {
		req.DisableVMD = p.IsVMDDisabled()
		return p.fwd.Format(req)
	}
	// set vmd state on remote provider in forwarded request
	if req.IsForwarded() && req.DisableVMD {
		p.disableVMD()
	}

	return p.backend.Format(req)
}
