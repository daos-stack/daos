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
package proto

import (
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// MockNvmeNamespace is a mock protobuf Namespace message used in tests for
// multiple packages.
func MockNvmeNamespace(varIdx ...int32) *ctlpb.NvmeController_Namespace {
	native := storage.MockNvmeNamespace(varIdx...)
	pb := new(NvmeNamespace)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockNvmeDeviceHealth is a mock protobuf Health message used in tests for
// multiple packages.
func MockNvmeDeviceHealth(varIdx ...int32) *ctlpb.NvmeController_Health {
	native := storage.MockNvmeDeviceHealth(varIdx...)
	pb := new(NvmeDeviceHealth)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockNvmeController is a mock protobuf Controller message used in tests for
// multiple packages (message contains repeated namespace field).
func MockNvmeController(varIdx ...int32) *ctlpb.NvmeController {
	native := storage.MockNvmeController(varIdx...)
	pb := new(NvmeController)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// NewMockNvmeController generates specific protobuf controller message
func NewMockNvmeController(
	pciAddr string, fwRev string, model string, serial string,
	nss []*ctlpb.NvmeController_Namespace, hs *ctlpb.NvmeController_Health) *ctlpb.NvmeController {

	return &ctlpb.NvmeController{
		Model:       model,
		Serial:      serial,
		Pciaddr:     pciAddr,
		Fwrev:       fwRev,
		Namespaces:  nss,
		Healthstats: hs,
	}
}

// MockScmModule is a mock protobuf Module message used in tests for
// multiple packages.
func MockScmModule() *ctlpb.ScmModule {
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

// MockScmMount is a mock protobuf Mount message used in tests for
// multiple packages.
func MockScmMount() *ctlpb.ScmMount {
	return &ctlpb.ScmMount{Mntpoint: "/mnt/daos"}
}

// MockPmemDevice is a mock protobuf PmemDevice used in tests for multiple
// packages.
func MockPmemDevice() *ctlpb.PmemDevice {
	return &ctlpb.PmemDevice{
		Uuid:     "abcd-1234-efgh-5678",
		Blockdev: "pmem1",
		Dev:      "namespace-1",
		Numanode: 1,
		Size:     3183575302144,
	}
}
