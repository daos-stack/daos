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

	"github.com/daos-stack/go-ipmctl/ipmctl"

	pb "github.com/daos-stack/daos/src/control/proto/mgmt"
)

// ScmmMap is a type alias for info on Storage Class Memory Modules
type ScmmMap map[int32]*pb.ScmModule

// scmStorage gives access to underlying storage interface implementation
// for accessing SCM devices (API) in addition to storage of device
// details.
// IpmCtl provides necessary methods to interact with Storage Class
// Memory modules through libipmctl via go-ipmctl bindings.
type scmStorage struct {
	ipmCtl      ipmctl.IpmCtl // ipmctl NVM API interface
	modules     ScmmMap
	initialized bool
}

func loadModules(mms []ipmctl.DeviceDiscovery) (ScmmMap, error) {
	pbMms := make(ScmmMap)
	for _, c := range mms {
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
		return nil, fmt.Errorf("loadModules: input contained duplicate keys")
	}
	return pbMms, nil
}

// Discover method implementation for scmStorage
func (s *scmStorage) Discover() error {
	if s.initialized {
		mms, err := s.ipmCtl.Discover()
		if err != nil {
			return err
		}
		pbMms, err := loadModules(mms)
		if err != nil {
			return err
		}
		s.modules = pbMms
		return nil
	}
	return fmt.Errorf("scm storage not initialized")
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

// newScmStorage creates a new instance of ScmStorage struct.
//
// NvmMgmt is the implementation of IpmCtl interface in go-ipmctl
func newScmStorage() *scmStorage {
	return &scmStorage{ipmCtl: &ipmctl.NvmMgmt{}}
}
