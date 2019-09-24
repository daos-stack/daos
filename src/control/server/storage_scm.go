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
	"strconv"

	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	MsgScmRebootRequired   = "A reboot is required to process new memory allocation goals."
	msgScmNoModules        = "no scm modules to prepare"
	msgScmNotInited        = "scm storage could not be accessed"
	msgScmAlreadyFormatted = "scm storage has already been formatted and " +
		"reformat not implemented"
	msgScmMountEmpty = "scm mount must be specified in config"
	msgScmBadDevList = "expecting one scm dcpm pmem device " +
		"per-server in config"
	msgScmDevEmpty          = "scm dcpm device list must contain path"
	msgScmClassNotSupported = "operation unsupported on scm class"
	msgIpmctlDiscoverFail   = "ipmctl module discovery"
	msgScmUpdateNotImpl     = "scm firmware update not supported"
)

// scmStorage gives access to underlying storage interface implementation
// for accessing SCM devices (API) in addition to storage of device
// details.
//
// IpmCtl provides necessary methods to interact with Storage Class
// Memory modules through libipmctl via ipmctl bindings.
type scmStorage struct {
	log         logging.Logger
	ext         External
	ipmctl      ipmctl.IpmCtl // ipmctl NVM API interface
	prep        PrepScm
	modules     types.ScmModules
	pmemDevs    types.PmemDevices
	initialized bool
	formatted   bool
}

// TODO: implement remaining methods for scmStorage
// func (s *scmStorage) Update(req interface{}) interface{} {return nil}
// func (s *scmStorage) BurnIn(req interface{}) (fioPath string, cmds []string, env string, err error) {
// return
// }

// Setup implementation for scmStorage providing initial device discovery
func (s *scmStorage) Setup() error {
	return s.Discover()
}

// Teardown implementation for scmStorage
func (s *scmStorage) Teardown() error {
	s.initialized = false
	return nil
}

// Prep configures pmem device files for SCM
func (s *scmStorage) Prep(state types.ScmState) (needsReboot bool, pmemDevs []pmemDev, err error) {
	return s.prep.Prep(state)
}

// PrepReset resets configuration of SCM
func (s *scmStorage) PrepReset(state types.ScmState) (needsReboot bool, err error) {
	return s.prep.PrepReset(state)
}

// Discover method implementation for scmStorage
func (s *scmStorage) Discover() error {
	if s.initialized {
		return nil
	}

	mms, err := s.ipmctl.Discover()
	if err != nil {
		return errors.WithMessage(err, msgIpmctlDiscoverFail)
	}
	s.modules = loadModules(mms)

	pmems, err := s.prep.GetNamespaces()
	if err != nil {
		return errors.WithMessage(err, msgIpmctlDiscoverFail)
	}
	s.pmemDevs = translatePmemDevices(pmems)

	s.initialized = true

	return nil
}

func loadModules(mms []ipmctl.DeviceDiscovery) (pbMms types.ScmModules) {
	for _, c := range mms {
		pbMms = append(
			pbMms,
			&ctlpb.ScmModule{
				Loc: &ctlpb.ScmModule_Location{
					Channel:    uint32(c.Channel_id),
					Channelpos: uint32(c.Channel_pos),
					Memctrlr:   uint32(c.Memory_controller_id),
					Socket:     uint32(c.Socket_id),
				},
				Physicalid: uint32(c.Physical_id),
				Capacity:   c.Capacity,
			})
	}
	return
}

// clearMount unmounts then removes mount point.
//
// NOTE: requires elevated privileges
func (s *scmStorage) clearMount(mntPoint string) (err error) {
	if err = s.ext.unmount(mntPoint); err != nil {
		return
	}

	if err = s.ext.remove(mntPoint); err != nil {
		return
	}

	return
}

// reFormat wipes fs signatures and formats dev with ext4.
//
// NOTE: Requires elevated privileges and is a destructive operation, prompt
//       user for confirmation before running.
func (s *scmStorage) reFormat(devPath string) (err error) {
	s.log.Debugf("wiping all fs identifiers on device %s", devPath)

	if err = s.ext.runCommand(
		fmt.Sprintf("wipefs -a %s", devPath)); err != nil {

		return errors.WithMessage(err, "wipefs")
	}

	if err = s.ext.runCommand(
		fmt.Sprintf("mkfs.ext4 %s", devPath)); err != nil {

		return errors.WithMessage(err, "mkfs format")
	}

	return
}

func getMntParams(cfg storage.ScmConfig) (mntType string, dev string, opts string, err error) {
	switch cfg.Class {
	case storage.ScmClassDCPM:
		mntType = "ext4"
		opts = "dax"
		if len(cfg.DeviceList) != 1 {
			err = errors.New(msgScmBadDevList)
			break
		}

		dev = cfg.DeviceList[0]
		if dev == "" {
			err = errors.New(msgScmDevEmpty)
		}
	case storage.ScmClassRAM:
		dev = "tmpfs"
		mntType = "tmpfs"

		if cfg.RamdiskSize >= 0 {
			opts = "size=" + strconv.Itoa(cfg.RamdiskSize) + "g"
		}
	default:
		err = errors.Errorf("%s: %s", cfg.Class, msgScmClassNotSupported)
	}

	return
}

// makeMount creates a mount target directory and mounts device there.
//
// NOTE: requires elevated privileges
func (s *scmStorage) makeMount(devPath string, mntPoint string, mntType string,
	mntOpts string) (err error) {

	if err = s.ext.mkdir(mntPoint); err != nil {
		return
	}

	if err = s.ext.mount(devPath, mntPoint, mntType, uintptr(0), mntOpts); err != nil {
		return
	}

	return
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
	mntPoint := cfg.MountPoint
	s.log.Debugf("performing SCM device reset, format and mount")

	// wraps around addMret to provide format specific function, ignore infoMsg
	addMretFormat := func(status ctlpb.ResponseStatus, errMsg string) {
		*results = append(*results,
			newMntRet(s.log, "format", mntPoint, status, errMsg, ""))
	}

	if !s.initialized {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, msgScmNotInited)
		return
	}

	if s.formatted {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, msgScmAlreadyFormatted)
		return
	}

	if mntPoint == "" {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_CONF, msgScmMountEmpty)
		return
	}

	mntType, devPath, mntOpts, err := getMntParams(cfg)
	if err != nil {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_CONF, err.Error())
		return
	}

	switch cfg.Class {
	case storage.ScmClassDCPM:
		if err := s.clearMount(mntPoint); err != nil {
			addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, err.Error())
			return
		}

		s.log.Debugf("formatting scm device %s, should be quick!...", devPath)

		if err := s.reFormat(devPath); err != nil {
			addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, err.Error())
			return
		}

		s.log.Debugf("scm format complete.\n")
	case storage.ScmClassRAM:
		if err := s.clearMount(mntPoint); err != nil {
			addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, err.Error())
			return
		}

		s.log.Debugf("no scm_size specified in config for ram tmpfs")
	}

	s.log.Debugf("mounting scm device %s at %s (%s)...", devPath, mntPoint, mntType)

	if err := s.makeMount(devPath, mntPoint, mntType, mntOpts); err != nil {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, err.Error())
		return
	}

	s.log.Debugf("scm mount complete.\n")
	addMretFormat(ctlpb.ResponseStatus_CTRL_SUCCESS, "")

	s.log.Debugf("SCM device reset, format and mount completed")
	s.formatted = true
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
//
// NvmMgmt is the implementation of ipmctl interface in ipmctl
func newScmStorage(log logging.Logger, ext External) *scmStorage {
	return &scmStorage{
		log:    log,
		ext:    ext,
		ipmctl: &ipmctl.NvmMgmt{},
		prep:   newPrepScm(log, run),
	}
}
