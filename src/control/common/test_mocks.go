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

package common

import (
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
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

// MockNamespacePB is a mock protobuf Namespace message used in tests for
// multiple packages.
func MockNamespacePB() *pb.NvmeNamespace {
	return &pb.NvmeNamespace{
		Id:       int32(12345),
		Capacity: int32(99999),
	}
}

// MockControllerPB is a mock protobuf Controller message used in tests for
// multiple packages (message contains repeated namespace field).
func MockControllerPB(fwRev string) *pb.NvmeController {
	return &pb.NvmeController{
		Model:     "ABC",
		Serial:    "123ABC",
		Pciaddr:   "1:2:3.0",
		Fwrev:     fwRev,
		Namespace: []*pb.NvmeNamespace{MockNamespacePB()},
	}
}

// MockModulePB is a mock protobuf Module message used in tests for
// multiple packages.
func MockModulePB() *pb.ScmModule {
	return &pb.ScmModule{
		Physicalid: uint32(12345),
		Channel:    uint32(1),
		Channelpos: uint32(2),
		Memctrlr:   uint32(3),
		Socket:     uint32(4),
		Capacity:   12345,
	}
}

// MockCheckMountOk mocks CheckMount and always returns nil error.
func MockCheckMountOk(path string) error {
	return nil
}
