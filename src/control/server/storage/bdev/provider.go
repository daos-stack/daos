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
	"bufio"
	"bytes"
	"os/exec"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	// InitRequest defines the parameters for initializing the provider.
	InitRequest struct {
		pbin.ForwardableRequest
		SPDKShmID int
	}

	// InitResponse contains the results of a successful Init operation.
	InitResponse struct{}

	// ScanRequest defines the parameters for a Scan operation.
	ScanRequest struct {
		pbin.ForwardableRequest
		EnableVmd bool
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
	}

	// DeviceFormatResponse contains device-specific Format operation results.
	DeviceFormatResponse struct {
		Formatted  bool
		Error      *fault.Fault
		Controller *storage.NvmeController
	}

	// DeviceFormatResponses is a map of device identifiers to device Format results.
	DeviceFormatResponses map[string]*DeviceFormatResponse

	// FormatResponse contains the results of a Format operation.
	FormatResponse struct {
		DeviceResponses DeviceFormatResponses
	}

	// Backend defines a set of methods to be implemented by a Block Device backend.
	Backend interface {
		Init(shmID ...int) error
		Reset() error
		Prepare(PrepareRequest) error
		Scan() (storage.NvmeControllers, error)
		Format(pciAddr string) (*storage.NvmeController, error)
		EnableVmd()
		IsVmdEnabled() bool
	}

	// Provider encapsulates configuration and logic for interacting with a Block
	// Device Backend.
	Provider struct {
		log     logging.Logger
		backend Backend
		fwd     *Forwarder
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
		fwd:     NewForwarder(log),
	}
}

// WithForwardingDisabled returns a provider with forwarding disabled.
func (p *Provider) WithForwardingDisabled() *Provider {
	p.fwd.Disabled = true
	return p
}

func (p *Provider) shouldForward(req pbin.ForwardChecker) bool {
	return !p.fwd.Disabled && !req.IsForwarded()
}

// EnableVmd turns on VMD device awareness.
func (p *Provider) EnableVmd() {
	p.backend.EnableVmd()
}

// IsVmdEnabled returns true if provider is VMD device aware.
func (p *Provider) IsVmdEnabled() bool {
	return p.backend.IsVmdEnabled()
}

// Init performs any initialization steps required by the provider.
func (p *Provider) Init(req InitRequest) error {
	if p.shouldForward(req) {
		return p.fwd.Init(req)
	}
	return p.backend.Init(req.SPDKShmID)
}

// Scan attempts to perform a scan to discover NVMe components in the system.
func (p *Provider) Scan(req ScanRequest) (*ScanResponse, error) {
	if p.shouldForward(req) {
		req.EnableVmd = p.IsVmdEnabled()
		return p.fwd.Scan(req)
	}
	if req.IsForwarded() && req.EnableVmd {
		p.EnableVmd()
	}

	cs, err := p.backend.Scan()
	if err != nil {
		return nil, err
	}

	return &ScanResponse{
		Controllers: cs,
	}, nil
}

// detectVmd returns whether VMD devices have been found and a slice of VMD
// PCI addresses if found.
func detectVmd() ([]string, error) {
	// Check available VMD devices with command:
	// "$lspci | grep  -i -E "201d | Volume Management Device"
	lspciCmd := exec.Command("lspci")
	vmdCmd := exec.Command("grep", "-i", "-E", "201d|Volume Management Device")
	var cmdOut bytes.Buffer

	vmdCmd.Stdin, _ = lspciCmd.StdoutPipe()
	vmdCmd.Stdout = &cmdOut
	_ = lspciCmd.Start()
	_ = vmdCmd.Run()
	_ = lspciCmd.Wait()

	if cmdOut.Len() == 0 {
		return []string{}, nil
	}

	vmdCount := bytes.Count(cmdOut.Bytes(), []byte("0000:"))
	vmdAddrs := make([]string, 0, vmdCount)

	i := 0
	scanner := bufio.NewScanner(&cmdOut)
	for scanner.Scan() {
		if i == vmdCount {
			break
		}
		s := strings.Split(scanner.Text(), " ")
		vmdAddrs = append(vmdAddrs, strings.TrimSpace(s[0]))
		i++
	}

	if len(vmdAddrs) == 0 {
		return nil, errors.New("error parsing cmd output")
	}

	return vmdAddrs, nil
}

// Prepare attempts to perform all actions necessary to make NVMe components available for
// use by DAOS.
func (p *Provider) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	if p.shouldForward(req) {
		return p.fwd.Prepare(req)
	}

	// run reset first to ensure reallocation of hugepages
	if err := p.backend.Reset(); err != nil {
		return nil, errors.WithMessage(err, "SPDK setup reset")
	}

	res := &PrepareResponse{}
	// if we're only resetting, just return before prep
	if req.ResetOnly {
		return res, nil
	}

	if err := p.backend.Prepare(req); err != nil {
		return nil, errors.WithMessage(err, "SPDK prepare")
	}

	if req.DisableVMD {
		return res, nil
	}

	vmdDevs, err := detectVmd()
	if err != nil {
		return nil, errors.Wrap(err, "VMD could not be enabled")
	}

	if len(vmdDevs) == 0 {
		return res, nil
	}

	vmdReq := req
	// If VMD devices are going to be used, then need to run a separate
	// bdev prepare (SPDK setup) with the VMD address as the PCI_WHITELIST
	// TODO: ignore devices not in include list
	req.PCIWhitelist = strings.Join(vmdDevs, " ")
	p.log.Debugf("VMD enabled, unbinding %v", vmdDevs)

	if err := p.backend.Prepare(vmdReq); err != nil {
		return nil, errors.WithMessage(err, "SPDK VMD prepare")
	}

	res.VmdDetected = true

	return res, nil
}

// Format attempts to initialize NVMe devices for use by DAOS (NB: no-op for non-NVMe devices).
func (p *Provider) Format(req FormatRequest) (*FormatResponse, error) {
	if len(req.DeviceList) == 0 {
		return nil, errors.New("empty DeviceList in FormatRequest")
	}

	if p.shouldForward(req) {
		return p.fwd.Format(req)
	}

	// TODO (DAOS-3844): Kick off device formats in goroutines? Serially formatting a large
	// number of NVMe devices can be slow.
	res := &FormatResponse{
		DeviceResponses: make(DeviceFormatResponses),
	}

	// TODO: fix for VMD
	for _, dev := range req.DeviceList {
		res.DeviceResponses[dev] = &DeviceFormatResponse{}
		switch req.Class {
		default:
			res.DeviceResponses[dev].Error = FaultFormatUnknownClass(req.Class.String())
		case storage.BdevClassKdev, storage.BdevClassFile, storage.BdevClassMalloc:
			res.DeviceResponses[dev].Formatted = true
			p.log.Infof("%s format for non-NVMe bdev skipped (%s)", req.Class, dev)
		case storage.BdevClassNvme:
			p.log.Infof("%s format starting (%s)", req.Class, dev)
			c, err := p.backend.Format(dev)
			if err != nil {
				p.log.Errorf("%s format failed (%s)", req.Class, dev)
				res.DeviceResponses[dev].Error = FaultFormatError(dev, err)
				continue
			}
			res.DeviceResponses[dev].Controller = c
			res.DeviceResponses[dev].Formatted = true
			p.log.Infof("%s format successful (%s)", req.Class, dev)
		}
	}

	return res, nil
}
