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
		sync.Mutex // ensure mutually exclusive access to scan cache
		log        logging.Logger
		backend    Backend
		scanCache  *storage.BdevScanResponse
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

func filterScanResp(resp *storage.BdevScanResponse, pciFilter ...string) (int, *storage.BdevScanResponse) {
	var skipped int
	out := make(storage.NvmeControllers, 0)

	if len(pciFilter) == 0 {
		return skipped, &storage.BdevScanResponse{Controllers: resp.Controllers}
	}

	for _, c := range resp.Controllers {
		if !common.Includes(pciFilter, c.PciAddr) {
			skipped++
			continue
		}
		out = append(out, c)
	}

	return skipped, &storage.BdevScanResponse{Controllers: out}
}

type scanFwdFn func(storage.BdevScanRequest) (*storage.BdevScanResponse, error)

func filterScan(req storage.BdevScanRequest, cache *storage.BdevScanResponse, scan scanFwdFn) (msg string, resp *storage.BdevScanResponse, update bool, err error) {
	var action string
	switch {
	case req.FlushCache:
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
		num, resp = filterScanResp(resp, req.DeviceList...)
		if num != 0 {
			msg += fmt.Sprintf("-%d filtered", num)
		}
	}

	msg += " devices)"

	return
}

// Scan attempts to perform a scan to discover NVMe components in the
// system. Results will be cached at the provider and returned if
// "FlushCache" is set to "false" in the request. Returned results will be
// filtered by request "DeviceList" and empty filter implies allowing all.
func (p *Provider) Scan(req storage.BdevScanRequest) (resp *storage.BdevScanResponse, err error) {
	// set vmd state on remote provider in forwarded request
	//	if req.IsForwarded() && req.DisableVMD {
	//		p.disableVMD()
	//	}

	p.Lock()
	defer p.Unlock()

	msg, resp, update, err := filterScan(req, p.scanCache, p.backend.Scan)
	p.log.Debug(msg)
	if update {
		p.scanCache = resp
	}

	return resp, err
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
