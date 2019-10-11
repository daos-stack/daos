//
// (C) Copyright 2018-2019 Intel Corporation.
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

package server

import (
	"fmt"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

const (
	MsgScmRebootRequired   = "A reboot is required to process new memory allocation goals."
	msgScmNoModules        = "no scm modules to prepare"
	msgScmNotInited        = "scm storage could not be accessed"
	msgScmAlreadyFormatted = "scm storage has already been formatted and " +
		"reformat not implemented"
	msgScmClassNotSupported = "operation unsupported on scm class"
	msgIpmctlDiscoverFail   = "ipmctl module discovery"
	msgScmUpdateNotImpl     = "scm firmware update not supported"
)

// scmStorage gives access to underlying storage interface implementation
// for accessing SCM devices (API) in addition to storage of device
// details.
//
type scmStorage struct {
	log         logging.Logger
	provider    *scm.Provider
	ext         External
	scanResults *scm.ScanResponse
	formatted   bool
}

// TODO: implement remaining methods for scmStorage
// func (s *scmStorage) Update(req interface{}) interface{} {return nil}
// func (s *scmStorage) BurnIn(req interface{}) (fioPath string, cmds []string, env string, err error) {
// return
// }

// Setup implementation for scmStorage providing initial device discovery
func (s *scmStorage) Setup() error {
	_, err := s.Scan()

	return err
}

// Teardown implementation for scmStorage
func (s *scmStorage) Teardown() error {
	s.scanResults = nil
	return nil
}

// Prep configures pmem device files for SCM
func (s *scmStorage) Prep() (needsReboot bool, namespaces []scm.Namespace, err error) {
	res, err := s.provider.Prepare(scm.PrepareRequest{})
	if err != nil {
		return
	}

	return res.RebootRequired, res.Namespaces, nil
}

// PrepReset resets configuration of SCM
func (s *scmStorage) PrepReset() (needsReboot bool, err error) {
	res, err := s.provider.Prepare(scm.PrepareRequest{Reset: true})
	if err != nil {
		return
	}

	return res.RebootRequired, nil
}

// Scan method implementation for scmStorage, delegate to provider.
func (s *scmStorage) Scan() (*scm.ScanResponse, error) {
	// always scan by default
	return s.provider.Scan(scm.ScanRequest{Rescan: true})
}

// newMntRet creates and populates NVMe ctrlr result and logs error through
// newState.
func newMntRet(log logging.Logger, op string, mntPoint string, status ctlpb.ResponseStatus, errMsg string,
	infoMsg string) *ctlpb.ScmMountResult {

	return &ctlpb.ScmMountResult{
		Mntpoint: mntPoint,
		State:    newState(log, status, errMsg, infoMsg, "scm mount "+op),
	}
}

// Format attempts to format (forcefully) SCM mounts on a given server
// as specified in config file and populates resp ScmMountResult.
func (s *scmStorage) Format(cfg storage.ScmConfig, results *(types.ScmMountResults)) {
	s.log.Debug("performing SCM device reset, format and mount")

	// wraps around addMret to provide format specific function, ignore infoMsg
	addMretFormat := func(status ctlpb.ResponseStatus, errMsg string) {
		*results = append(*results,
			newMntRet(s.log, "format", cfg.MountPoint, status, errMsg, ""))
	}

	if s.formatted {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, msgScmAlreadyFormatted)
		return
	}

	if s.scanResults == nil {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, msgScmNotInited)
		return
	}

	req := scm.FormatRequest{
		Mountpoint: cfg.MountPoint,
	}

	switch cfg.Class {
	case storage.ScmClassDCPM:
		// FIXME (DAOS-3291): Clean up SCM configuration
		if len(cfg.DeviceList) != 1 {
			addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, scm.FaultFormatInvalidDeviceCount.Error())
			return
		}
		req.Dcpm = &scm.DcpmParams{
			Device: cfg.DeviceList[0],
		}
	case storage.ScmClassRAM:
		req.Ramdisk = &scm.RamdiskParams{
			Size: uint(cfg.RamdiskSize),
		}
	default:
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_CONF,
			fmt.Sprintf("%s: %s", cfg.Class, msgScmClassNotSupported))
		return
	}

	res, err := s.provider.CheckFormat(req)
	if err != nil {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, err.Error())
		return
	}
	if res.Formatted {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, msgScmAlreadyFormatted)
		return
	}

	res, err = s.provider.Format(req)
	if err != nil {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, err.Error())
		return
	}

	addMretFormat(ctlpb.ResponseStatus_CTRL_SUCCESS, "")

	s.log.Debug("SCM device reset, format and mount completed")
	s.formatted = res.Formatted
}

// Update is currently a placeholder method stubbing SCM module fw update.
func (s *scmStorage) Update(
	cfg storage.ScmConfig, req *ctlpb.UpdateScmReq, results *(types.ScmModuleResults)) {

	// respond with single result indicating no implementation
	*results = append(
		*results,
		&ctlpb.ScmModuleResult{
			Loc: &ctlpb.ScmModule_Location{},
			State: newState(s.log, ctlpb.ResponseStatus_CTRL_NO_IMPL,
				msgScmUpdateNotImpl, "", "scm module update"),
		})
}

// newScmStorage creates a new instance of ScmStorage struct.
func newScmStorage(log logging.Logger, ext External) *scmStorage {
	return &scmStorage{
		log:      log,
		provider: scm.DefaultProvider(log),
		ext:      ext,
	}
}
