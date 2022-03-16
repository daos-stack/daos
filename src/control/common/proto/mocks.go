//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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

// MockSmdDevice is a mock protobuf SmdDevice message used in tests for
// multiple packages.
func MockSmdDevice(parentTrAddr string, varIdx ...int32) *ctlpb.NvmeController_SmdDevice {
	native := storage.MockSmdDevice(parentTrAddr, varIdx...)
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
