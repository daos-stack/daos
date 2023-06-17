//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package proto

import (
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/dustin/go-humanize"
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
func MockSmdDevice(parentTrAddr string, varIdx ...int32) *ctlpb.SmdDevice {
	native := storage.MockSmdDevice(parentTrAddr, varIdx...)
	pb := new(SmdDevice)

	if err := pb.FromNative(native); err != nil {
		panic(err)
	}

	return pb.AsProto()
}

// MockNvmeHealth is a mock protobuf Health message used in tests for
// multiple packages.
func MockNvmeHealth(varIdx ...int32) *ctlpb.BioHealthResp {
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
	{Uuid: "12345678-1234-1234-1234-123456789abc", SvcReps: []uint32{1, 2}},
	{Uuid: "12345678-1234-1234-1234-cba987654321", SvcReps: []uint32{0}},
}

// MockPBMemInfo returns a mock MemInfo result.
func MockPBMemInfo() *ctlpb.MemInfo {
	return &ctlpb.MemInfo{
		HugepagesTotal:    1024,
		HugepagesFree:     512,
		HugepagesReserved: 64,
		HugepagesSurplus:  32,
		HugepageSizeKb:    2048,
		MemTotalKb:        (humanize.GiByte * 4) / humanize.KiByte,
		MemFreeKb:         (humanize.GiByte * 1) / humanize.KiByte,
		MemAvailableKb:    (humanize.GiByte * 2) / humanize.KiByte,
	}
}
