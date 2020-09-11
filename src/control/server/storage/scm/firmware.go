//
// (C) Copyright 2020 Intel Corporation.
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
// +build firmware

package scm

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	// FirmwareQueryMethod is the method name used when forwarding the request
	// to query SCM firmware.
	FirmwareQueryMethod = "ScmFirmwareQuery"
	// FirmwareUpdateMethod is the method name used when forwarding the request
	// to update SCM firmware.
	FirmwareUpdateMethod = "ScmFirmwareUpdate"
)

type (
	// firmwareProvider is an embedded structure that enables a Provider to
	// forward firmware requests to a privileged binary if firmware management
	// is enabled in the build.
	firmwareProvider struct {
		fwFwd *FirmwareForwarder
	}

	// FirmwareQueryRequest defines the parameters for a firmware query.
	FirmwareQueryRequest struct {
		pbin.ForwardableRequest
		Devices []string // requested device UIDs, empty for all
	}

	// ModuleFirmware represents the results of a firmware query for a specific
	// SCM module.
	ModuleFirmware struct {
		Module storage.ScmModule
		Info   *storage.ScmFirmwareInfo
		Error  string
	}

	// FirmwareQueryResponse contains the results of a successful firmware query.
	FirmwareQueryResponse struct {
		Results []ModuleFirmware
	}

	// FirmwareUpdateRequest defines the parameters for a firmware update.
	FirmwareUpdateRequest struct {
		pbin.ForwardableRequest
		Devices      []string // requested device UIDs, empty for all
		FirmwarePath string   // location of the firmware binary
	}

	// ModuleFirmwareUpdateResult represents the result of a firmware update for
	// a specific SCM module.
	ModuleFirmwareUpdateResult struct {
		Module storage.ScmModule
		Error  string
	}

	// FirmwareUpdateResponse contains the results of the firmware update.
	FirmwareUpdateResponse struct {
		Results []ModuleFirmwareUpdateResult
	}
)

// setupFirmwareProvider initializes the provider's firmware capabilities.
func (p *Provider) setupFirmwareProvider(log logging.Logger) {
	p.fwFwd = NewFirmwareForwarder(log)
}

// QueryFirmware fetches the status of SCM device firmware.
func (p *Provider) QueryFirmware(req FirmwareQueryRequest) (*FirmwareQueryResponse, error) {
	if p.shouldForward(req) {
		return p.fwFwd.Query(req)
	}

	modules, err := p.getRequestedModules(req.Devices)
	if err != nil {
		return nil, err
	}

	resp := &FirmwareQueryResponse{
		Results: make([]ModuleFirmware, 0, len(modules)),
	}
	for _, mod := range modules {
		fwInfo, err := p.backend.GetFirmwareStatus(mod.UID)
		result := ModuleFirmware{
			Module: *mod,
			Info:   fwInfo,
		}
		if err != nil {
			result.Error = err.Error()
		}
		resp.Results = append(resp.Results, result)
	}

	return resp, nil
}

// UpdateFirmware updates the SCM device firmware.
func (p *Provider) UpdateFirmware(req FirmwareUpdateRequest) (*FirmwareUpdateResponse, error) {
	if p.shouldForward(req) {
		return p.fwFwd.Update(req)
	}

	if len(req.FirmwarePath) == 0 {
		return nil, errors.New("missing path to firmware file")
	}

	modules, err := p.getRequestedModules(req.Devices)
	if err != nil {
		return nil, err
	}

	if len(modules) == 0 {
		return nil, errors.New("no SCM modules")
	}

	resp := &FirmwareUpdateResponse{
		Results: make([]ModuleFirmwareUpdateResult, 0, len(modules)),
	}
	for _, mod := range modules {
		err = p.backend.UpdateFirmware(mod.UID, req.FirmwarePath)
		result := ModuleFirmwareUpdateResult{
			Module: *mod,
		}
		if err != nil {
			result.Error = err.Error()
		}
		resp.Results = append(resp.Results, result)
	}

	return resp, nil
}

// FirmwareForwarder forwards firmware requests to a privileged binary.
type FirmwareForwarder struct {
	pbin.Forwarder
}

// NewFirmwareForwarder returns a new FirmwareForwarder.
func NewFirmwareForwarder(log logging.Logger) *FirmwareForwarder {
	pf := pbin.NewForwarder(log, pbin.DaosFWName)

	return &FirmwareForwarder{
		Forwarder: *pf,
	}
}

// checkSupport verifies that the firmware support binary is installed.
func (f *FirmwareForwarder) checkSupport() error {
	if f.Forwarder.CanForward() {
		return nil
	}

	return errors.Errorf("SCM firmware operations are not supported on this system")
}

// Query forwards an SCM firmware query request.
func (f *FirmwareForwarder) Query(req FirmwareQueryRequest) (*FirmwareQueryResponse, error) {
	if err := f.checkSupport(); err != nil {
		return nil, err
	}
	req.Forwarded = true

	res := new(FirmwareQueryResponse)
	if err := f.SendReq(FirmwareQueryMethod, req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Update forwards a request to update firmware on the SCM.
func (f *FirmwareForwarder) Update(req FirmwareUpdateRequest) (*FirmwareUpdateResponse, error) {
	if err := f.checkSupport(); err != nil {
		return nil, err
	}
	req.Forwarded = true

	res := new(FirmwareUpdateResponse)
	if err := f.SendReq(FirmwareUpdateMethod, req, res); err != nil {
		return nil, err
	}

	return res, nil
}
