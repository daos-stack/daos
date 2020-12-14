//
// (C) Copyright 2019-2020 Intel Corporation.
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
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
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

// MockSmdDevice is a mock protobuf SmdDevice message used in tests for
// multiple packages.
func MockSmdDevice(varIdx ...int32) *ctlpb.NvmeController_SmdDevice {
	native := storage.MockSmdDevice(varIdx...)
	pb := new(SmdDevice)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockNvmeHealth is a mock protobuf Health message used in tests for
// multiple packages.
func MockNvmeHealth(varIdx ...int32) *ctlpb.NvmeController_Health {
	native := storage.MockNvmeHealth(varIdx...)
	pb := new(NvmeHealth)

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

// MockScmModule generates specific protobuf SCM module message used in tests
// for multiple packages.
func MockScmModule(varIdx ...int32) *ctlpb.ScmModule {
	native := storage.MockScmModule(varIdx...)
	pb := new(ScmModule)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockScmNamespace generates specific protobuf SCM namespace message used in tests
// for multiple packages.
func MockScmNamespace(varIdx ...int32) *ctlpb.ScmNamespace {
	native := storage.MockScmNamespace(varIdx...)
	pb := new(ScmNamespace)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockScmMountPoint generates specific protobuf SCM namespace mount message
// used in tests for multiple packages.
func MockScmMountPoint(varIdx ...int32) *ctlpb.ScmNamespace_Mount {
	native := storage.MockScmMountPoint(varIdx...)
	pb := new(ScmMountPoint)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockPoolList returns a slice of mock protobuf Pool messages used in tests for
// multiple packages.
var MockPoolList = []*mgmtpb.ListPoolsResp_Pool{
	{Uuid: "12345678-1234-1234-1234-123456789abc", Svcreps: []uint32{1, 2}},
	{Uuid: "12345678-1234-1234-1234-cba987654321", Svcreps: []uint32{0}},
}
