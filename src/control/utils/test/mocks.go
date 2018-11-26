//
// (C) Copyright 2018 Intel Corporation.
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

// Package testutils contains utility functions for unit tests
package testutils

import (
	pb "github.com/daos-stack/daos/src/control/mgmt/proto"
	"github.com/daos-stack/go-ipmctl/ipmctl"
	"github.com/daos-stack/go-spdk/spdk"
)

// MockFeaturePB is a mock protobuf Feature message used in tests for multiple
// packages.
func MockFeaturePB() *pb.Feature {
	return &pb.Feature{
		Category:    &pb.Category{Category: "nvme"},
		Fname:       &pb.FeatureName{Name: "burn-name"},
		Description: "run workloads on device to test",
	}
}

// MockController is a mock NVMe SSD controller of type exported from go-spdk.
func MockController(fwrev string) spdk.Controller {
	return spdk.Controller{
		ID:      int32(12345),
		Model:   "ABC",
		Serial:  "123ABC",
		PCIAddr: "1:2:3.0",
		FWRev:   fwrev,
	}
}

// MockNamespace is a mock NVMe SSD namespace of type exported from go-spdk.
func MockNamespace(ctrlr *spdk.Controller) spdk.Namespace {
	return spdk.Namespace{
		ID:      ctrlr.ID,
		Size:    int32(99999),
		CtrlrID: int32(12345),
	}
}

// MockNamespacePB is a mock protobuf Namespace message used in tests for
// multiple packages.
func MockNamespacePB(c spdk.Controller) *pb.NvmeNamespace {
	ns := MockNamespace(&c)
	return &pb.NvmeNamespace{
		Id:       ns.ID,
		Capacity: ns.Size,
	}
}

// MockControllerPB is a mock protobuf Controller message used in tests for
// multiple packages (message contains repeated namespace field).
func MockControllerPB(fwRev string) *pb.NvmeController {
	c := MockController(fwRev)
	return &pb.NvmeController{
		Id:        c.ID,
		Model:     c.Model,
		Serial:    c.Serial,
		Pciaddr:   c.PCIAddr,
		Fwrev:     c.FWRev,
		Namespace: []*pb.NvmeNamespace{MockNamespacePB(c)},
	}
}

// MockModule is a mock SCM module of type exported from go-ipmctl.
func MockModule() ipmctl.DeviceDiscovery {
	return ipmctl.DeviceDiscovery{}
	// todo: create with some example field data.
	// ID:      int32(12345),
	// Model:   "ABC",
	// Serial:  "123ABC",
	// FWRev:   fwrev,
	// }
}

// MockModulePB is a mock protobuf Module message used in tests for
// multiple packages.
func MockModulePB() *pb.ScmModule {
	c := MockModule()
	return &pb.ScmModule{
		Physicalid: uint32(c.Physical_id),
		Channel:    uint32(c.Channel_id),
		Channelpos: uint32(c.Channel_pos),
		Memctrlr:   uint32(c.Memory_controller_id),
		Socket:     uint32(c.Socket_id),
		Capacity:   c.Capacity,
	}
}

// MockCheckMount mocks CheckMount and always returns nil error.
func MockCheckMountOk(path string) error {
	return nil
}
