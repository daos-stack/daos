//
// (C) Copyright 2019 Intel Corporation.
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
package scm

import (
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

// AdminForwarder forwards requests to the DAOS admin binary.
type AdminForwarder struct {
	pbin.Forwarder
}

// NewAdminForwarder creates a new AdminForwarder.
func NewAdminForwarder(log logging.Logger) *AdminForwarder {
	pf := pbin.NewForwarder(log, pbin.DaosAdminName)

	return &AdminForwarder{
		Forwarder: *pf,
	}
}

// Mount forwards an SCM mount request.
func (f *AdminForwarder) Mount(req MountRequest) (*MountResponse, error) {
	req.Forwarded = true

	res := new(MountResponse)
	if err := f.SendReq("ScmMount", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Unmount forwards an SCM unmount request.
func (f *AdminForwarder) Unmount(req MountRequest) (*MountResponse, error) {
	req.Forwarded = true

	res := new(MountResponse)
	if err := f.SendReq("ScmUnmount", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Format forwards a request request to format SCM.
func (f *AdminForwarder) Format(req FormatRequest) (*FormatResponse, error) {
	req.Forwarded = true

	res := new(FormatResponse)
	if err := f.SendReq("ScmFormat", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// CheckFormat forwards a request to check the SCM formatting.
func (f *AdminForwarder) CheckFormat(req FormatRequest) (*FormatResponse, error) {
	req.Forwarded = true

	res := new(FormatResponse)
	if err := f.SendReq("ScmCheckFormat", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Scan forwards an SCM scan request.
func (f *AdminForwarder) Scan(req ScanRequest) (*ScanResponse, error) {
	req.Forwarded = true

	res := new(ScanResponse)
	if err := f.SendReq("ScmScan", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Prepare forwards a request to prep the SCM.
func (f *AdminForwarder) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	req.Forwarded = true

	res := new(PrepareResponse)
	if err := f.SendReq("ScmPrepare", req, res); err != nil {
		return nil, err
	}

	return res, nil
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
	if _, err := common.FindBinary(f.Forwarder.GetBinaryName()); os.IsNotExist(err) {
		return errors.Errorf("SCM firmware operations are not supported on this system")
	}

	return nil
}

// Query forwards an SCM firmware query request.
func (f *FirmwareForwarder) Query(req FirmwareQueryRequest) (*FirmwareQueryResponse, error) {
	if err := f.checkSupport(); err != nil {
		return nil, err
	}
	req.Forwarded = true

	res := new(FirmwareQueryResponse)
	if err := f.SendReq("ScmFirmwareQuery", req, res); err != nil {
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
	if err := f.SendReq("ScmFirmwareUpdate", req, res); err != nil {
		return nil, err
	}

	return res, nil
}
