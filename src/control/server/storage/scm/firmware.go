//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build firmware

package scm

import (
	"fmt"
	"sort"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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
		DeviceUIDs  []string // requested device UIDs, empty for all
		ModelID     string   // filter by model ID
		FirmwareRev string   // filter by current FW revision
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
		DeviceUIDs   []string // requested device UIDs, empty for all
		FirmwarePath string   // location of the firmware binary
		ModelID      string   // filter devices by model ID
		FirmwareRev  string   // filter devices by current FW revision
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

	// For query we won't complain about bad IDs
	modules, err := p.getRequestedModules(req.DeviceUIDs, true)
	if err != nil {
		return nil, err
	}
	modules = filterModules(modules, req.FirmwareRev, req.ModelID)

	resp := &FirmwareQueryResponse{
		Results: make([]ModuleFirmware, len(modules)),
	}
	for i, mod := range modules {
		fwInfo, err := p.backend.GetFirmwareStatus(mod.UID)
		resp.Results[i].Module = *mod
		resp.Results[i].Info = fwInfo
		if err != nil {
			resp.Results[i].Error = err.Error()
		}
	}

	return resp, nil
}

func (p *Provider) getRequestedModules(requestedUIDs []string, ignoreMissing bool) (storage.ScmModules, error) {
	modules, err := p.backend.Discover()
	if err != nil {
		return nil, err
	}

	if !ignoreMissing && len(modules) == 0 {
		return nil, errors.New("no SCM modules")
	}

	if len(requestedUIDs) == 0 {
		return modules, nil
	}

	if common.StringSliceHasDuplicates(requestedUIDs) {
		return nil, FaultDuplicateDevices
	}
	sort.Strings(requestedUIDs)

	result := make(storage.ScmModules, 0, len(modules))
	for _, uid := range requestedUIDs {
		mod, err := getModule(uid, modules)
		if err != nil {
			if ignoreMissing {
				continue
			}
			return nil, err
		}
		result = append(result, mod)
	}

	return result, nil
}

func getModule(uid string, modules storage.ScmModules) (*storage.ScmModule, error) {
	for _, mod := range modules {
		if mod.UID == uid {
			return mod, nil
		}
	}

	return nil, fmt.Errorf("no module found with UID %q", uid)
}

func filterModules(modules storage.ScmModules, fwRev string, modelID string) storage.ScmModules {
	filtered := make(storage.ScmModules, 0, len(modules))
	for _, mod := range modules {
		if common.FilterStringMatches(fwRev, mod.FirmwareRevision) &&
			common.FilterStringMatches(modelID, mod.PartNumber) {
			filtered = append(filtered, mod)
		}
	}
	return filtered
}

// UpdateFirmware updates the SCM device firmware.
func (p *Provider) UpdateFirmware(req FirmwareUpdateRequest) (*FirmwareUpdateResponse, error) {
	if p.shouldForward(req) {
		return p.fwFwd.Update(req)
	}

	if len(req.FirmwarePath) == 0 {
		return nil, errors.New("missing path to firmware file")
	}

	modules, err := p.getRequestedModules(req.DeviceUIDs, false)
	if err != nil {
		return nil, err
	}
	modules = filterModules(modules, req.FirmwareRev, req.ModelID)

	if len(modules) == 0 {
		return nil, FaultNoFilterMatch
	}

	resp := &FirmwareUpdateResponse{
		Results: make([]ModuleFirmwareUpdateResult, len(modules)),
	}
	for i, mod := range modules {
		err = p.backend.UpdateFirmware(mod.UID, req.FirmwarePath)
		resp.Results[i].Module = *mod
		if err != nil {
			resp.Results[i].Error = err.Error()
		}
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
