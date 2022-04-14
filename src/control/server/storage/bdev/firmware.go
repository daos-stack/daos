//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	// defaultFirmwareSlot is the slot automatically chosen for the firmware
	// update
	defaultFirmwareSlot = 0
)

// QueryFirmware requests the firmware information for the NVMe device controller.
func (p *Provider) QueryFirmware(req storage.NVMeFirmwareQueryRequest) (*storage.NVMeFirmwareQueryResponse, error) {
	// For the time being this just scans and returns the devices, which include their
	// firmware revision.
	controllers, err := p.getRequestedControllers(req.DeviceAddrs, req.ModelID, req.FirmwareRev, true)
	if err != nil {
		return nil, err
	}

	results := make([]storage.NVMeDeviceFirmwareQueryResult, len(controllers))
	for i, dev := range controllers {
		results[i].Device = *dev
	}
	resp := &storage.NVMeFirmwareQueryResponse{
		Results: results,
	}

	return resp, nil
}

func (p *Provider) getRequestedControllers(requestedPCIAddrs []string, modelID string, fwRev string, ignoreMissing bool) (storage.NvmeControllers, error) {
	controllers, err := p.getRequestedControllersByAddr(requestedPCIAddrs, ignoreMissing)
	if err != nil {
		return nil, err
	}
	filtered := filterControllersByModelFirmware(controllers, modelID, fwRev)
	if !ignoreMissing && len(filtered) == 0 {
		return nil, FaultNoFilterMatch
	}
	return filtered, nil
}

func (p *Provider) getRequestedControllersByAddr(requestedPCIAddrs []string, ignoreMissing bool) (storage.NvmeControllers, error) {
	resp, err := p.Scan(storage.BdevScanRequest{})
	if err != nil {
		return nil, err
	}

	if !ignoreMissing && len(resp.Controllers) == 0 {
		return nil, errors.New("no NVMe device controllers")
	}

	if len(requestedPCIAddrs) == 0 {
		return resp.Controllers, nil
	}

	if common.StringSliceHasDuplicates(requestedPCIAddrs) {
		return nil, FaultDuplicateDevices
	}

	result := make(storage.NvmeControllers, 0, len(requestedPCIAddrs))
	for _, addr := range requestedPCIAddrs {
		dev, err := getDeviceController(addr, resp.Controllers)
		if err != nil {
			if ignoreMissing {
				continue
			}
			return nil, err
		}
		result = append(result, dev)
	}

	return result, nil
}

func getDeviceController(pciAddr string, controllers storage.NvmeControllers) (*storage.NvmeController, error) {
	for _, dev := range controllers {
		if dev.PciAddr == pciAddr {
			return dev, nil
		}
	}

	return nil, storage.FaultBdevNotFound(pciAddr)
}

func filterControllersByModelFirmware(controllers storage.NvmeControllers, modelID, fwRev string) storage.NvmeControllers {
	selected := make(storage.NvmeControllers, 0, len(controllers))
	for _, ctrlr := range controllers {
		if common.FilterStringMatches(modelID, ctrlr.Model) &&
			common.FilterStringMatches(fwRev, ctrlr.FwRev) {
			selected = append(selected, ctrlr)
		}
	}
	return selected
}

// UpdateFirmware updates the NVMe device controller firmware.
func (p *Provider) UpdateFirmware(req storage.NVMeFirmwareUpdateRequest) (*storage.NVMeFirmwareUpdateResponse, error) {
	if len(req.FirmwarePath) == 0 {
		return nil, errors.New("missing path to firmware file")
	}

	controllers, err := p.getRequestedControllers(req.DeviceAddrs, req.ModelID, req.FirmwareRev, false)
	if err != nil {
		return nil, err
	}

	if len(controllers) == 0 {
		return nil, errors.New("no NVMe device controllers")
	}

	resp := &storage.NVMeFirmwareUpdateResponse{
		Results: make([]storage.NVMeDeviceFirmwareUpdateResult, len(controllers)),
	}
	for i, con := range controllers {
		err = p.backend.UpdateFirmware(con.PciAddr, req.FirmwarePath, defaultFirmwareSlot)
		resp.Results[i].Device = *con
		if err != nil {
			resp.Results[i].Error = err.Error()
		}
	}

	return resp, nil
}
