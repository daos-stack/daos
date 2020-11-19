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

package bdev

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	// FirmwareUpdateMethod is the name of the method used to forward the request to
	// update NVMe device firmware.
	FirmwareUpdateMethod = "NvmeFirmwareUpdate"

	// defaultFirmwareSlot is the slot automatically chosen for the firmware
	// update
	defaultFirmwareSlot = 0
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
		DeviceAddrs []string // requested device PCI addresses, empty for all
		ModelID     string   // filter devices by model ID
		FirmwareRev string   // filter devices by current FW revision
	}

	// DeviceFirmwareQueryResult represents the result of a firmware query for
	// a specific NVMe controller.
	DeviceFirmwareQueryResult struct {
		Device storage.NvmeController
	}

	// FirmwareQueryResponse contains the results of the firmware query.
	FirmwareQueryResponse struct {
		Results []DeviceFirmwareQueryResult
	}

	// FirmwareUpdateRequest defines the parameters for a firmware update.
	FirmwareUpdateRequest struct {
		pbin.ForwardableRequest
		DeviceAddrs  []string // requested device PCI addresses, empty for all
		FirmwarePath string   // location of the firmware binary
		ModelID      string   // filter devices by model ID
		FirmwareRev  string   // filter devices by current FW revision
	}

	// DeviceFirmwareUpdateResult represents the result of a firmware update for
	// a specific NVMe controller.
	DeviceFirmwareUpdateResult struct {
		Device storage.NvmeController
		Error  string
	}

	// FirmwareUpdateResponse contains the results of the firmware update.
	FirmwareUpdateResponse struct {
		Results []DeviceFirmwareUpdateResult
	}
)

// setupFirmwareProvider sets up the firmware provider.
func (p *Provider) setupFirmwareProvider(log logging.Logger) {
	p.fwFwd = NewFirmwareForwarder(log)
}

// QueryFirmware requests the firmware information for the NVMe device controller.
func (p *Provider) QueryFirmware(req FirmwareQueryRequest) (*FirmwareQueryResponse, error) {
	// For the time being this just scans and returns the devices, which include their
	// firmware revision.
	controllers, err := p.getRequestedControllers(req.DeviceAddrs, req.ModelID, req.FirmwareRev, true)
	if err != nil {
		return nil, err
	}

	results := make([]DeviceFirmwareQueryResult, len(controllers))
	for i, dev := range controllers {
		results[i].Device = *dev
	}
	resp := &FirmwareQueryResponse{
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
	resp, err := p.backend.Scan(ScanRequest{})
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

	return nil, FaultPCIAddrNotFound(pciAddr)
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
func (p *Provider) UpdateFirmware(req FirmwareUpdateRequest) (*FirmwareUpdateResponse, error) {
	if p.shouldForward(req) {
		return p.fwFwd.Update(req)
	}

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

	resp := &FirmwareUpdateResponse{
		Results: make([]DeviceFirmwareUpdateResult, len(controllers)),
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

// FirmwareForwarder forwards firmware requests to a privileged binary.
type FirmwareForwarder struct {
	pbin.Forwarder
}

// NewFirmwareForwarder returns a new bdev FirmwareForwarder.
func NewFirmwareForwarder(log logging.Logger) *FirmwareForwarder {
	pf := pbin.NewForwarder(log, pbin.DaosFWName)

	return &FirmwareForwarder{
		Forwarder: *pf,
	}
}

// checkSupport verifies that the firmware support binary is installed.
func (f *FirmwareForwarder) checkSupport() error {
	if f.CanForward() {
		return nil
	}

	return errors.Errorf("NVMe firmware operations are not supported on this system")
}

// Update forwards a request to update firmware on the NVMe device.
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
