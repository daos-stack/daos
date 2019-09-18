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

package storage

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/server/storage/config"
	. "github.com/daos-stack/daos/src/control/server/storage/messages"
)

const (
	msgUnmount      = "syscall: calling unmount with %s, MNT_DETACH"
	msgMount        = "syscall: mount %s, %s, %s, %s, %s"
	msgIsMountPoint = "check if dir %s is mounted"
	msgMkdir        = "os: mkdirall %s, 0777"
	msgRemove       = "os: removeall %s"
	msgCmd          = "cmd: %s"
)

type scmExt interface {
	runCommand(cmdString string) error
	mkdir(mntPoint string) error
	remove(mntPoint string) error
	isMountPoint(mntPoint string) (bool, error)
	mount(devPath, mntPoint, mntType string, flags uintptr, opts string) error
	unmount(mount string) error
}

type scmExtImpl struct {
	sync.RWMutex
	history []string

	log logging.Logger
}

func errPermsAnnotate(err error) (e error) {
	e = err
	if os.IsPermission(e) {
		e = errors.WithMessagef(
			e,
			"%s requires elevated privileges to perform this action",
			os.Args[0])
	}
	return
}

func (e *scmExtImpl) appendHistory(str string) {
	e.Lock()
	defer e.Unlock()
	e.history = append(e.history, str)
}

func (e *scmExtImpl) getHistory() []string {
	e.RLock()
	defer e.RUnlock()
	return e.history
}

func (e *scmExtImpl) runCommand(cmdString string) error {
	e.appendHistory(fmt.Sprintf(msgCmd, cmdString))
	return common.Run(cmdString)
}

func (e *scmExtImpl) isMountPoint(path string) (bool, error) {
	e.log.Debugf(msgIsMountPoint, path)
	e.appendHistory(fmt.Sprintf(msgIsMountPoint, path))

	pStat, err := os.Stat(path)
	if err != nil {
		return false, err
	}

	rStat, err := os.Stat(filepath.Dir(strings.TrimSuffix(path, "/")))
	if err != nil {
		return false, err
	}

	if pStat.Sys().(*syscall.Stat_t).Dev == rStat.Sys().(*syscall.Stat_t).Dev {
		return false, nil
	}

	// if root dir has different parent device than path then probably a mountpoint
	return true, nil
}

func (e *scmExtImpl) mkdir(path string) error {
	e.log.Debugf(msgMkdir, path)
	e.appendHistory(fmt.Sprintf(msgMkdir, path))

	if err := os.MkdirAll(path, 0777); err != nil {
		return errPermsAnnotate(errors.WithMessage(err, "mkdir"))
	}
	return nil
}

func (e *scmExtImpl) mount(devPath, mntPoint, mntType string, flags uintptr, opts string) error {
	op := fmt.Sprintf(msgMount, devPath, mntPoint, mntType, fmt.Sprint(flags), opts)

	e.log.Debugf(op)
	e.appendHistory(op)

	if flags == 0 {
		flags = uintptr(syscall.MS_NOATIME | syscall.MS_SILENT)
		flags |= syscall.MS_NODEV | syscall.MS_NOEXEC | syscall.MS_NOSUID
	}

	if err := syscall.Mount(devPath, mntPoint, mntType, flags, opts); err != nil {
		return errPermsAnnotate(os.NewSyscallError("mount", err))
	}
	return nil
}

func (e *scmExtImpl) unmount(path string) error {
	e.log.Debugf(msgUnmount, path)
	e.appendHistory(fmt.Sprintf(msgUnmount, path))

	// ignore NOENT errors, treat as success
	if err := syscall.Unmount(
		path, syscall.MNT_DETACH); err != nil && !os.IsNotExist(err) {

		// when mntpoint exists but is unmounted, get EINVAL
		e, ok := err.(syscall.Errno)
		if ok && e == syscall.EINVAL {
			return nil
		}

		return errPermsAnnotate(os.NewSyscallError("umount", err))
	}
	return nil
}

func (e *scmExtImpl) remove(path string) error {
	e.log.Debugf(msgRemove, path)
	e.appendHistory(fmt.Sprintf(msgRemove, path))

	// ignore NOENT errors, treat as success
	if err := os.RemoveAll(path); err != nil && !os.IsNotExist(err) {
		return errPermsAnnotate(errors.WithMessage(err, "remove"))
	}
	return nil
}

func defaultScmExt(log logging.Logger) *scmExtImpl {
	return &scmExtImpl{
		log:     log,
		history: []string{},
	}
}

// ScmProvider gives access to underlying storage interface implementation
// for accessing SCM devices (API) in addition to storage of device
// details.
//
// IpmCtl provides necessary methods to interact with Storage Class
// Memory modules through libipmctl via ipmctl bindings.
type ScmProvider struct {
	log         logging.Logger
	ext         scmExt
	ipmctl      ipmctl.IpmCtl // ipmctl NVM API interface
	prep        PrepScm
	modules     types.ScmModules
	pmemDevs    types.PmemDevices
	initialized bool
	formatted   bool
}

// TODO: implement remaining methods for ScmProvider
// func (s *ScmProvider) Update(req interface{}) interface{} {return nil}
// func (s *ScmProvider) BurnIn(req interface{}) (fioPath string, cmds []string, env string, err error) {
// return
// }

// Setup implementation for ScmProvider providing initial device discovery
func (s *ScmProvider) Setup() error {
	return s.Discover()
}

// Teardown implementation for ScmProvider
func (s *ScmProvider) Teardown() error {
	s.initialized = false
	return nil
}

// Prep configures pmem device files for SCM
func (s *ScmProvider) Prep(state types.ScmState) (needsReboot bool, PmemDevs []PmemDev, err error) {
	return s.prep.Prep(state)
}

// PrepReset resets configuration of SCM
func (s *ScmProvider) PrepReset(state types.ScmState) (needsReboot bool, err error) {
	return s.prep.PrepReset(state)
}

// Discover method implementation for ScmProvider
func (s *ScmProvider) Discover() error {
	if s.initialized {
		return nil
	}

	mms, err := s.ipmctl.Discover()
	if err != nil {
		return errors.WithMessage(err, MsgIpmctlDiscoverFail)
	}
	s.modules = loadModules(mms)

	pmems, err := s.prep.GetNamespaces()
	if err != nil {
		return errors.WithMessage(err, MsgIpmctlDiscoverFail)
	}
	s.pmemDevs = TranslatePmemDevices(pmems)

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
func (s *ScmProvider) clearMount(mntPoint string) (err error) {
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
func (s *ScmProvider) reFormat(devPath string) (err error) {
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

func getMntParams(cfg ScmConfig) (mntType string, dev string, opts string, err error) {
	switch cfg.Class {
	case ScmClassDCPM:
		mntType = "ext4"
		opts = "dax"
		if len(cfg.DeviceList) != 1 {
			err = errors.New(MsgScmBadDevList)
			break
		}

		dev = cfg.DeviceList[0]
		if dev == "" {
			err = errors.New(MsgScmDevEmpty)
		}
	case ScmClassRAM:
		dev = "tmpfs"
		mntType = "tmpfs"

		if cfg.RamdiskSize >= 0 {
			opts = "size=" + strconv.Itoa(cfg.RamdiskSize) + "g"
		}
	default:
		err = errors.Errorf("%s: %s", cfg.Class, MsgScmClassNotSupported)
	}

	return
}

// makeMount creates a mount target directory and mounts device there.
//
// NOTE: requires elevated privileges
func (s *ScmProvider) makeMount(devPath string, mntPoint string, mntType string,
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
		State:    NewState(log, status, errMsg, infoMsg, "scm mount "+op),
	}
}

// Format attempts to format (forcefully) SCM mounts on a given server
// as specified in config file and populates resp ScmMountResult.
func (s *ScmProvider) Format(cfg ScmConfig, results *(types.ScmMountResults)) {
	s.log.Debugf("performing SCM device reset, format and mount")
	mntPoint := cfg.MountPoint

	// wraps around addMret to provide format specific function, ignore infoMsg
	addMretFormat := func(status ctlpb.ResponseStatus, errMsg string) {
		*results = append(*results,
			newMntRet(s.log, "format", mntPoint, status, errMsg, ""))
	}

	if !s.initialized {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, MsgScmNotInited)
		return
	}

	if s.formatted {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, MsgScmAlreadyFormatted)
		return
	}

	if mntPoint == "" {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_CONF, MsgScmMountEmpty)
		return
	}

	mntType, devPath, mntOpts, err := getMntParams(cfg)
	if err != nil {
		addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_CONF, err.Error())
		return
	}

	switch cfg.Class {
	case ScmClassDCPM:
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
	case ScmClassRAM:
		if err := s.clearMount(mntPoint); err != nil {
			addMretFormat(ctlpb.ResponseStatus_CTRL_ERR_APP, err.Error())
			return
		}

		if cfg.RamdiskSize == 0 {
			s.log.Debugf("no scm_size specified in config for ram tmpfs")
		}
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
func (s *ScmProvider) Update(
	cfg ScmConfig, req *ctlpb.UpdateScmReq, results *(types.ScmModuleResults)) {

	// respond with single result indicating no implementation
	*results = append(
		*results,
		&ctlpb.ScmModuleResult{
			Loc: &ctlpb.ScmModule_Location{},
			State: NewState(s.log, ctlpb.ResponseStatus_CTRL_NO_IMPL,
				MsgScmUpdateNotImpl, "", "scm module update"),
		})
}

func (s *ScmProvider) SetFormatted(f bool) {
	s.formatted = f
}

func (s *ScmProvider) Formatted() bool {
	return s.formatted
}

func (s *ScmProvider) GetPrepState() (types.ScmState, error) {
	return s.prep.GetState()
}

func (s *ScmProvider) Initialized() bool {
	return s.initialized
}

func (s *ScmProvider) Modules() types.ScmModules {
	return s.modules
}

func (s *ScmProvider) PmemDevs() types.PmemDevices {
	return s.pmemDevs
}

func (s *ScmProvider) Mount(cfg ScmConfig) error {
	s.log.Debugf("checking mount: %s", cfg.MountPoint)
	isMount, err := s.ext.isMountPoint(cfg.MountPoint)
	if err != nil && !os.IsNotExist(errors.Cause(err)) {
		return errors.WithMessage(err, "failed to check SCM mount")
	}
	if isMount {
		s.log.Debugf("%s already mounted", cfg.MountPoint)
		return nil
	}

	s.log.Debugf("attempting to mount existing SCM dir %s\n", cfg.MountPoint)
	mntType, devPath, mntOpts, err := getMntParams(cfg)
	if err != nil {
		return errors.WithMessage(err, "getting scm mount params")
	}

	s.log.Debugf("mounting scm %s at %s (%s)...", devPath, cfg.MountPoint, mntType)
	if err := s.ext.mount(devPath, cfg.MountPoint, mntType, uintptr(0), mntOpts); err != nil {
		return errors.WithMessage(err, "mounting existing scm dir")
	}

	return nil
}

func (s *ScmProvider) IsMounted(cfg ScmConfig) (bool, error) {
	return s.ext.isMountPoint(cfg.MountPoint)
}

// NewScmProvider creates a new instance of ScmStorage struct.
//
// NvmMgmt is the implementation of ipmctl interface in ipmctl
func NewScmProvider(log logging.Logger) *ScmProvider {
	return &ScmProvider{
		log:    log,
		ext:    defaultScmExt(log),
		ipmctl: &ipmctl.NvmMgmt{},
		prep:   newPrepScm(log, run),
	}
}
