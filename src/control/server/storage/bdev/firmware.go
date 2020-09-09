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
	"fmt"
	"sort"

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

	// FirmwareUpdateRequest defines the parameters for a firmware update.
	FirmwareUpdateRequest struct {
		pbin.ForwardableRequest
		DeviceAddrs  []string // requested device PCI addresses, empty for all
		FirmwarePath string   // location of the firmware binary
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

// UpdateFirmware updates the NVMe device controller firmware.
func (p *Provider) UpdateFirmware(req FirmwareUpdateRequest) (*FirmwareUpdateResponse, error) {
	if p.shouldForward(req) {
		return p.fwFwd.Update(req)
	}

	if len(req.FirmwarePath) == 0 {
		return nil, errors.New("missing path to firmware file")
	}

	controllers, err := p.getRequestedControllers(req.DeviceAddrs)
	if err != nil {
		return nil, err
	}

	if len(controllers) == 0 {
		return nil, errors.New("no NVMe device controllers")
	}

	resp := &FirmwareUpdateResponse{
		Results: make([]DeviceFirmwareUpdateResult, 0, len(controllers)),
	}
	for _, con := range controllers {
		err = p.backend.UpdateFirmware(con.PciAddr, req.FirmwarePath, defaultFirmwareSlot)
		result := DeviceFirmwareUpdateResult{
			Device: *con,
		}
		if err != nil {
			result.Error = err.Error()
		}
		resp.Results = append(resp.Results, result)
	}

	return resp, nil
}

func (p *Provider) getRequestedControllers(requestedPCIAddrs []string) (storage.NvmeControllers, error) {
	resp, err := p.backend.Scan(ScanRequest{})
	if err != nil {
		return nil, err
	}

	if len(requestedPCIAddrs) == 0 {
		return resp.Controllers, nil
	}

	uniquePCIAddrs := common.DedupeStringSlice(requestedPCIAddrs)
	sort.Strings(uniquePCIAddrs)

	result := make(storage.NvmeControllers, 0, len(uniquePCIAddrs))
	for _, addr := range uniquePCIAddrs {
		dev, err := getDeviceController(addr, resp.Controllers)
		if err != nil {
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

	return nil, fmt.Errorf("no NVMe controller found with PCI address %q", pciAddr)
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
