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

import pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"

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
func MockNamespacePB() *pb.NvmeController_Namespace {
	return &pb.NvmeController_Namespace{
		Id:       int32(12345),
		Capacity: int32(99999),
	}
}

// MockDeviceHealthPB is a mock protobuf Health message used in tests for
// multiple packages.
func MockDeviceHealthPB() *pb.NvmeController_Health {
	return &pb.NvmeController_Health{
		Temp:		uint32(300),
		Tempwarn:	uint32(0),
		Tempcrit:	uint32(0),
		Ctrlbusy:	uint64(0),
		Powercycles:	uint64(99),
		Poweronhours:	uint64(9999),
		Unsafeshutdowns: uint64(1),
		Mediaerrors:	uint64(0),
		Errorlogs:	uint64(0),
		Tempwarning:	false,
		Availspare:	false,
		Reliability:	false,
		Readonly:	false,
		Volatilemem:	false,
	}
}

// MockControllerPB is a mock protobuf Controller message used in tests for
// multiple packages (message contains repeated namespace field).
func MockControllerPB(fwRev string) *pb.NvmeController {
	return &pb.NvmeController{
		Model:       "ABC",
		Serial:      "123ABC",
		Pciaddr:     "0000:81:00.0",
		Fwrev:       fwRev,
		Namespaces:  []*pb.NvmeController_Namespace{MockNamespacePB()},
		Healthstats: []*pb.NvmeController_Health{MockDeviceHealthPB()},
	}
}

// NewMockControllerPB generates specific protobuf controller message
func NewMockControllerPB(
	pciAddr string, fwRev string, model string, serial string,
	nss []*pb.NvmeController_Namespace, dh []*pb.NvmeController_Health) *pb.NvmeController {

	return &pb.NvmeController{
		Model:      model,
		Serial:     serial,
		Pciaddr:    pciAddr,
		Fwrev:      fwRev,
		Namespaces: nss,
		Healthstats: dh,
	}
}

// MockModulePB is a mock protobuf Module message used in tests for
// multiple packages.
func MockModulePB() *pb.ScmModule {
	return &pb.ScmModule{
		Physicalid: uint32(12345),
		Capacity:   12345,
		Loc: &pb.ScmModule_Location{
			Channel:    uint32(1),
			Channelpos: uint32(2),
			Memctrlr:   uint32(3),
			Socket:     uint32(4),
		},
	}
}

// MockMountPB is a mock protobuf Mount message used in tests for
// multiple packages.
func MockMountPB() *pb.ScmMount {
	// MockModulePB is a mock protobuf Module message used in tests for
	return &pb.ScmMount{Mntpoint: "/mnt/daos"}
}

// MockPmemDevicePB is a mock protobuf PmemDevice used in tests for multiple
// packages.
func MockPmemDevicePB() *pb.PmemDevice {
	return &pb.PmemDevice{
		Uuid:     "abcd-1234-efgh-5678",
		Blockdev: "pmem1",
		Dev:      "namespace-1",
		Numanode: 1,
	}
}

// MockCheckMountOk mocks CheckMount and always returns nil error.
func MockCheckMountOk(path string) error {
	return nil
}
