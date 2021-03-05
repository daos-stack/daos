//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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
		NoCache    bool
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

func (resp *ScanResponse) filter(pciFilter ...string) (int, *ScanResponse) {
	var skipped int
	out := make(storage.NvmeControllers, 0)

	if len(pciFilter) == 0 {
		return skipped, &ScanResponse{Controllers: resp.Controllers}
	}

	for _, c := range resp.Controllers {
		if !common.Includes(pciFilter, c.PciAddr) {
			skipped++
			continue
		}
		out = append(out, c)
	}

	return skipped, &ScanResponse{Controllers: out}
}

type scanFwdFn func(ScanRequest) (*ScanResponse, error)

func forwardScan(req ScanRequest, cache *ScanResponse, scan scanFwdFn) (msg string, resp *ScanResponse, update bool, err error) {
	var action string
	switch {
	case req.NoCache:
		action = "bypass"
		resp, err = scan(req)
	case cache != nil && len(cache.Controllers) != 0:
		action = "reuse"
		resp = cache
	default:
		action = "update"
		resp, err = scan(req)
		if err == nil && resp != nil {
			update = true
		}
	}

	msg = fmt.Sprintf("bdev scan: %s cache", action)

	if err != nil {
		return
	}

	if resp == nil {
		err = errors.New("unexpected nil response from bdev backend")
		return
	}

	msg += fmt.Sprintf(" (%d", len(resp.Controllers))
	if len(req.DeviceList) != 0 && len(resp.Controllers) != 0 {
		var num int
		num, resp = resp.filter(req.DeviceList...)
		if num != 0 {
			msg += fmt.Sprintf("-%d filtered", num)
		}
	}

	msg += " devices)"

	return
}

// Scan attempts to perform a scan to discover NVMe components in the
// system. Results will be cached at the provider and returned if
// "NoCache" is set to "false" in the request. Returned results will be
// filtered by request "DeviceList" and empty filter implies allowing all.
func (p *Provider) Scan(req ScanRequest) (resp *ScanResponse, err error) {
	if p.shouldForward(req) {
		req.DisableVMD = p.IsVMDDisabled()

		p.Lock()
		defer p.Unlock()

		msg, resp, update, err := forwardScan(req, p.scanCache, p.fwd.Scan)
		p.log.Debug(msg)
		if update {
			p.scanCache = resp
		}

		return resp, err
	}

	// set vmd state on remote provider in forwarded request
	if req.IsForwarded() && req.DisableVMD {
		p.disableVMD()
	}

	return p.backend.Scan(req)
}

// Prepare attempts to perform all actions necessary to make NVMe
// components available for use by DAOS.
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

// Format attempts to initialize NVMe devices for use by DAOS.
// Note that this is a no-op for non-NVMe devices.
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
