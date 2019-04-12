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

package main

import (
	"fmt"
	"strconv"
	"syscall"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/daos-stack/go-ipmctl/ipmctl"
	"github.com/pkg/errors"
)

// ScmmMap is a type alias for info on Storage Class Memory Modules
type ScmmMap map[int32]*pb.ScmModule

// ScmStorage interface specifies basic functionality for subsystem
type ScmStorage interface {
	Setup() error
	Teardown() error
	Format(int) error
	Discover() error
}

// scmStorage gives access to underlying storage interface implementation
// for accessing SCM devices (API) in addition to storage of device
// details.
//
// IpmCtl provides necessary methods to interact with Storage Class
// Memory modules through libipmctl via go-ipmctl bindings.
type scmStorage struct {
	ipmCtl      ipmctl.IpmCtl  // ipmctl NVM API interface
	config      *configuration // server configuration structure
	modules     ScmmMap
	initialized bool
	formatted   bool
}

// todo: implement remaining methods for scmStorage
// func (s *scmStorage) Update(params interface{}) interface{} {return nil}
// func (s *scmStorage) BurnIn(params interface{}) (fioPath string, cmds []string, env string, err error) {
// return
// }

// Setup placeholder implementation for scmStorage
func (s *scmStorage) Setup() error {
	s.initialized = true
	return nil
}

// Teardown placeholder implementation for scmStorage
func (s *scmStorage) Teardown() error {
	s.initialized = false
	return nil
}

func loadModules(mms []ipmctl.DeviceDiscovery) (ScmmMap, error) {
	pbMms := make(ScmmMap)
	for _, c := range mms {
		// can cast Physical_id to int32 (as is required to be a map
		// index) because originally is uint16 so no possibility of
		// sign bit corruption.
		pbMms[int32(c.Physical_id)] = &pb.ScmModule{
			Physicalid: uint32(c.Physical_id),
			Channel:    uint32(c.Channel_id),
			Channelpos: uint32(c.Channel_pos),
			Memctrlr:   uint32(c.Memory_controller_id),
			Socket:     uint32(c.Socket_id),
			Capacity:   c.Capacity,
		}
	}
	if len(pbMms) != len(mms) {
		return nil, errors.Errorf("loadModules: input contained duplicate keys")
	}
	return pbMms, nil
}

// Discover method implementation for scmStorage
func (s *scmStorage) Discover() error {
	if s.initialized {
		mms, err := s.ipmCtl.Discover()
		if err != nil {
			// TODO: check and handle permissions and no module errs
			//       to give caller useful message.
			return err
		}
		pbMms, err := loadModules(mms)
		if err != nil {
			return err
		}
		s.modules = pbMms
		return nil
	}
	return errors.Errorf("scm storage not initialized")
}

// clearMount unmounts then removes mount point.
//
// NOTE: requires elevated privileges
func (s *scmStorage) clearMount(mntPoint string) (err error) {
	if err = s.config.ext.unmount(mntPoint); err != nil {
		return
	}

	if err = s.config.ext.remove(mntPoint); err != nil {
		return
	}

	return
}

// reFormat wipes fs signatures and formats dev with ext4.
//
// NOTE: Requires elevated privileges and is a destructive operation, prompt
//       user for confirmation before running.
func (s *scmStorage) reFormat(devPath string) (err error) {
	log.Debugf("wiping all fs identifiers on device %s", devPath)

	if err = s.config.ext.runCommand(
		fmt.Sprintf("wipefs -a %s", devPath)); err != nil {

		return errors.WithMessage(err, "wipefs")
	}

	if err = s.config.ext.runCommand(
		fmt.Sprintf("mkfs.ext4 %s", devPath)); err != nil {

		return errors.WithMessage(err, "mkfs format")
	}

	return
}

// makeMount creates a mount target directory and mounts device there.
//
// NOTE: requires elevated privileges
func (s *scmStorage) makeMount(
	devPath string, mntPoint string, devType string, mntOpts string) (err error) {

	var flags uintptr
	flags = syscall.MS_NOATIME | syscall.MS_SILENT
	flags |= syscall.MS_NODEV | syscall.MS_NOEXEC | syscall.MS_NOSUID

	if err = s.config.ext.mkdir(mntPoint); err != nil {
		return
	}

	if err = s.config.ext.mount(
		devPath, mntPoint, devType, flags, mntOpts); err != nil {

		return
	}

	return
}

// Format attempts to format (forcefully) SCM devices on a given server
// as specified in config file.
func (s *scmStorage) Format(idx int) error {
	var devType, devPath, mntOpts string

	if s.formatted {
		return errors.New(
			"scm storage has already been formatted and reformat " +
				"not implemented")
	}

	srv := s.config.Servers[idx]
	mntPoint := srv.ScmMount

	if mntPoint == "" {
		return errors.New("scm mount must be specified in config")
	}
	if err := s.clearMount(mntPoint); err != nil {
		return err
	}

	switch srv.ScmClass {
	case scmDCPM:
		if len(srv.ScmList) != 1 {
			return errors.New(
				"expecting one scm dcpm pmem device per-server " +
					"in config")
		}
		devPath = srv.ScmList[0]
		if devPath == "" {
			return errors.New("scm dcpm device list must contain path")
		}
		devType = "ext4"
		mntOpts = "dax"

		if err := s.reFormat(devPath); err != nil {
			return err
		}
	case scmRAM:
		devPath = "tmpfs"
		devType = "tmpfs"

		if srv.ScmSize >= 0 {
			mntOpts = "size=" + strconv.Itoa(srv.ScmSize) + "g"
			break
		}
		log.Debugf("no scm_size specified in config for ram tmpfs")
	default:
		return errors.New("unsupported ScmClass")
	}

	if err := s.makeMount(devPath, mntPoint, devType, mntOpts); err != nil {
		return err
	}

	s.formatted = true
	return nil
}

// newScmStorage creates a new instance of ScmStorage struct.
//
// NvmMgmt is the implementation of IpmCtl interface in go-ipmctl
func newScmStorage(config *configuration) *scmStorage {
	return &scmStorage{
		ipmCtl: &ipmctl.NvmMgmt{},
		config: config,
	}
}
