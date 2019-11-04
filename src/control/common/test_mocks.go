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

import ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"

// MockNamespacePB is a mock protobuf Namespace message used in tests for
// multiple packages.
func MockNamespacePB() *ctlpb.NvmeController_Namespace {
	return &ctlpb.NvmeController_Namespace{
		Id:       int32(12345),
		Capacity: int32(99999),
	}
}

// MockDeviceHealthPB is a mock protobuf Health message used in tests for
// multiple packages.
func MockDeviceHealthPB() *ctlpb.NvmeController_Health {
	return &ctlpb.NvmeController_Health{
		Temp:            uint32(300),
		Tempwarn:        uint32(0),
		Tempcrit:        uint32(0),
		Ctrlbusy:        uint64(0),
		Powercycles:     uint64(99),
		Poweronhours:    uint64(9999),
		Unsafeshutdowns: uint64(1),
		Mediaerrors:     uint64(0),
		Errorlogs:       uint64(0),
		Tempwarning:     false,
		Availspare:      false,
		Reliability:     false,
		Readonly:        false,
		Volatilemem:     false,
	}
}

// MockControllerPB is a mock protobuf Controller message used in tests for
// multiple packages (message contains repeated namespace field).
func MockControllerPB() *ctlpb.NvmeController {
	return &ctlpb.NvmeController{
		Model:       "ABC",
		Serial:      "123ABC",
		Pciaddr:     "0000:81:00.0",
		Fwrev:       "1.0.0",
		Healthstats: MockDeviceHealthPB(),
		Namespaces:  []*ctlpb.NvmeController_Namespace{MockNamespacePB()},
	}
}

// NewMockControllerPB generates specific protobuf controller message
func NewMockControllerPB(pciAddr string, fwRev string, model string, serial string, numa int32,
	nss []*ctlpb.NvmeController_Namespace, hs *ctlpb.NvmeController_Health) *ctlpb.NvmeController {

	return &ctlpb.NvmeController{
		Model:       model,
		Serial:      serial,
		Pciaddr:     pciAddr,
		Fwrev:       fwRev,
		Socketid:    numa,
		Namespaces:  nss,
		Healthstats: hs,
	}
}

// MockModulePB is a mock protobuf Module message used in tests for
// multiple packages.
func MockModulePB() *ctlpb.ScmModule {
	return &ctlpb.ScmModule{
		Physicalid: uint32(12345),
		Capacity:   12345,
		Loc: &ctlpb.ScmModule_Location{
			Channel:    uint32(1),
			Channelpos: uint32(2),
			Memctrlr:   uint32(3),
			Socket:     uint32(4),
		},
	}
}

// MockMountPB is a mock protobuf Mount message used in tests for
// multiple packages.
func MockMountPB() *ctlpb.ScmMount {
	// MockModulePB is a mock protobuf Module message used in tests for
	return &ctlpb.ScmMount{Mntpoint: "/mnt/daos"}
}

// MockPmemDevicePB is a mock protobuf PmemDevice used in tests for multiple
// packages.
func MockPmemDevicePB() *ctlpb.PmemDevice {
	return &ctlpb.PmemDevice{
		Uuid:     "abcd-1234-efgh-5678",
		Blockdev: "pmem1",
		Dev:      "namespace-1",
		Numanode: 1,
		Size:     3183575302144,
	}
}

// MockCheckMountOk mocks CheckMount and always returns nil error.
func MockCheckMountOk(path string) error {
	return nil
}
