//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"sort"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// QueryFirmware fetches the status of SCM device firmware.
func (p *Provider) QueryFirmware(req storage.ScmFirmwareQueryRequest) (*storage.ScmFirmwareQueryResponse, error) {
	// For query we won't complain about bad IDs
	modules, err := p.getRequestedModules(req.DeviceUIDs, true)
	if err != nil {
		return nil, err
	}
	modules = filterModules(modules, req.FirmwareRev, req.ModelID)

	resp := &storage.ScmFirmwareQueryResponse{
		Results: make([]storage.ScmModuleFirmware, len(modules)),
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
	modules, err := p.backend.getModules()
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
func (p *Provider) UpdateFirmware(req storage.ScmFirmwareUpdateRequest) (*storage.ScmFirmwareUpdateResponse, error) {
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

	resp := &storage.ScmFirmwareUpdateResponse{
		Results: make([]storage.ScmFirmwareUpdateResult, len(modules)),
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
