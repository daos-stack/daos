//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.27.1-devel
// 	protoc        v3.5.0
// source: ctl/ctl.proto

package ctl

import (
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

var File_ctl_ctl_proto protoreflect.FileDescriptor

var file_ctl_ctl_proto_rawDesc = []byte{
	0x0a, 0x0d, 0x63, 0x74, 0x6c, 0x2f, 0x63, 0x74, 0x6c, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12,
	0x03, 0x63, 0x74, 0x6c, 0x1a, 0x11, 0x63, 0x74, 0x6c, 0x2f, 0x73, 0x74, 0x6f, 0x72, 0x61, 0x67,
	0x65, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x11, 0x63, 0x74, 0x6c, 0x2f, 0x6e, 0x65, 0x74,
	0x77, 0x6f, 0x72, 0x6b, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x12, 0x63, 0x74, 0x6c, 0x2f,
	0x66, 0x69, 0x72, 0x6d, 0x77, 0x61, 0x72, 0x65, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x0d,
	0x63, 0x74, 0x6c, 0x2f, 0x73, 0x6d, 0x64, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x0f, 0x63,
	0x74, 0x6c, 0x2f, 0x72, 0x61, 0x6e, 0x6b, 0x73, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x10,
	0x63, 0x74, 0x6c, 0x2f, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x32, 0xf4, 0x05, 0x0a, 0x06, 0x43, 0x74, 0x6c, 0x53, 0x76, 0x63, 0x12, 0x3a, 0x0a, 0x0b, 0x53,
	0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x53, 0x63, 0x61, 0x6e, 0x12, 0x13, 0x2e, 0x63, 0x74, 0x6c,
	0x2e, 0x53, 0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x53, 0x63, 0x61, 0x6e, 0x52, 0x65, 0x71, 0x1a,
	0x14, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x53, 0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x53, 0x63, 0x61,
	0x6e, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x40, 0x0a, 0x0d, 0x53, 0x74, 0x6f, 0x72, 0x61,
	0x67, 0x65, 0x46, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x12, 0x15, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x53,
	0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x46, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x52, 0x65, 0x71, 0x1a,
	0x16, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x53, 0x74, 0x6f, 0x72, 0x61, 0x67, 0x65, 0x46, 0x6f, 0x72,
	0x6d, 0x61, 0x74, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3e, 0x0a, 0x11, 0x53, 0x74, 0x6f,
	0x72, 0x61, 0x67, 0x65, 0x4e, 0x76, 0x6d, 0x65, 0x52, 0x65, 0x62, 0x69, 0x6e, 0x64, 0x12, 0x12,
	0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x4e, 0x76, 0x6d, 0x65, 0x52, 0x65, 0x62, 0x69, 0x6e, 0x64, 0x52,
	0x65, 0x71, 0x1a, 0x13, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x4e, 0x76, 0x6d, 0x65, 0x52, 0x65, 0x62,
	0x69, 0x6e, 0x64, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3a, 0x0a, 0x0b, 0x4e, 0x65, 0x74,
	0x77, 0x6f, 0x72, 0x6b, 0x53, 0x63, 0x61, 0x6e, 0x12, 0x13, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x4e,
	0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x53, 0x63, 0x61, 0x6e, 0x52, 0x65, 0x71, 0x1a, 0x14, 0x2e,
	0x63, 0x74, 0x6c, 0x2e, 0x4e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x53, 0x63, 0x61, 0x6e, 0x52,
	0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x40, 0x0a, 0x0d, 0x46, 0x69, 0x72, 0x6d, 0x77, 0x61, 0x72,
	0x65, 0x51, 0x75, 0x65, 0x72, 0x79, 0x12, 0x15, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x46, 0x69, 0x72,
	0x6d, 0x77, 0x61, 0x72, 0x65, 0x51, 0x75, 0x65, 0x72, 0x79, 0x52, 0x65, 0x71, 0x1a, 0x16, 0x2e,
	0x63, 0x74, 0x6c, 0x2e, 0x46, 0x69, 0x72, 0x6d, 0x77, 0x61, 0x72, 0x65, 0x51, 0x75, 0x65, 0x72,
	0x79, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x43, 0x0a, 0x0e, 0x46, 0x69, 0x72, 0x6d, 0x77,
	0x61, 0x72, 0x65, 0x55, 0x70, 0x64, 0x61, 0x74, 0x65, 0x12, 0x16, 0x2e, 0x63, 0x74, 0x6c, 0x2e,
	0x46, 0x69, 0x72, 0x6d, 0x77, 0x61, 0x72, 0x65, 0x55, 0x70, 0x64, 0x61, 0x74, 0x65, 0x52, 0x65,
	0x71, 0x1a, 0x17, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x46, 0x69, 0x72, 0x6d, 0x77, 0x61, 0x72, 0x65,
	0x55, 0x70, 0x64, 0x61, 0x74, 0x65, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x31, 0x0a, 0x08,
	0x53, 0x6d, 0x64, 0x51, 0x75, 0x65, 0x72, 0x79, 0x12, 0x10, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x53,
	0x6d, 0x64, 0x51, 0x75, 0x65, 0x72, 0x79, 0x52, 0x65, 0x71, 0x1a, 0x11, 0x2e, 0x63, 0x74, 0x6c,
	0x2e, 0x53, 0x6d, 0x64, 0x51, 0x75, 0x65, 0x72, 0x79, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12,
	0x40, 0x0a, 0x11, 0x53, 0x65, 0x74, 0x45, 0x6e, 0x67, 0x69, 0x6e, 0x65, 0x4c, 0x6f, 0x67, 0x4d,
	0x61, 0x73, 0x6b, 0x73, 0x12, 0x13, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x53, 0x65, 0x74, 0x4c, 0x6f,
	0x67, 0x4d, 0x61, 0x73, 0x6b, 0x73, 0x52, 0x65, 0x71, 0x1a, 0x14, 0x2e, 0x63, 0x74, 0x6c, 0x2e,
	0x53, 0x65, 0x74, 0x4c, 0x6f, 0x67, 0x4d, 0x61, 0x73, 0x6b, 0x73, 0x52, 0x65, 0x73, 0x70, 0x22,
	0x00, 0x12, 0x34, 0x0a, 0x11, 0x50, 0x72, 0x65, 0x70, 0x53, 0x68, 0x75, 0x74, 0x64, 0x6f, 0x77,
	0x6e, 0x52, 0x61, 0x6e, 0x6b, 0x73, 0x12, 0x0d, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e,
	0x6b, 0x73, 0x52, 0x65, 0x71, 0x1a, 0x0e, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e, 0x6b,
	0x73, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x2c, 0x0a, 0x09, 0x53, 0x74, 0x6f, 0x70, 0x52,
	0x61, 0x6e, 0x6b, 0x73, 0x12, 0x0d, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e, 0x6b, 0x73,
	0x52, 0x65, 0x71, 0x1a, 0x0e, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e, 0x6b, 0x73, 0x52,
	0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x2c, 0x0a, 0x09, 0x50, 0x69, 0x6e, 0x67, 0x52, 0x61, 0x6e,
	0x6b, 0x73, 0x12, 0x0d, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e, 0x6b, 0x73, 0x52, 0x65,
	0x71, 0x1a, 0x0e, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e, 0x6b, 0x73, 0x52, 0x65, 0x73,
	0x70, 0x22, 0x00, 0x12, 0x33, 0x0a, 0x10, 0x52, 0x65, 0x73, 0x65, 0x74, 0x46, 0x6f, 0x72, 0x6d,
	0x61, 0x74, 0x52, 0x61, 0x6e, 0x6b, 0x73, 0x12, 0x0d, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61,
	0x6e, 0x6b, 0x73, 0x52, 0x65, 0x71, 0x1a, 0x0e, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e,
	0x6b, 0x73, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x2d, 0x0a, 0x0a, 0x53, 0x74, 0x61, 0x72,
	0x74, 0x52, 0x61, 0x6e, 0x6b, 0x73, 0x12, 0x0d, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e,
	0x6b, 0x73, 0x52, 0x65, 0x71, 0x1a, 0x0e, 0x2e, 0x63, 0x74, 0x6c, 0x2e, 0x52, 0x61, 0x6e, 0x6b,
	0x73, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x42, 0x39, 0x5a, 0x37, 0x67, 0x69, 0x74, 0x68, 0x75,
	0x62, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2d, 0x73, 0x74, 0x61, 0x63, 0x6b,
	0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2f, 0x73, 0x72, 0x63, 0x2f, 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f,
	0x6c, 0x2f, 0x63, 0x6f, 0x6d, 0x6d, 0x6f, 0x6e, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63,
	0x74, 0x6c, 0x62, 0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var file_ctl_ctl_proto_goTypes = []interface{}{
	(*StorageScanReq)(nil),     // 0: ctl.StorageScanReq
	(*StorageFormatReq)(nil),   // 1: ctl.StorageFormatReq
	(*NvmeRebindReq)(nil),      // 2: ctl.NvmeRebindReq
	(*NetworkScanReq)(nil),     // 3: ctl.NetworkScanReq
	(*FirmwareQueryReq)(nil),   // 4: ctl.FirmwareQueryReq
	(*FirmwareUpdateReq)(nil),  // 5: ctl.FirmwareUpdateReq
	(*SmdQueryReq)(nil),        // 6: ctl.SmdQueryReq
	(*SetLogMasksReq)(nil),     // 7: ctl.SetLogMasksReq
	(*RanksReq)(nil),           // 8: ctl.RanksReq
	(*StorageScanResp)(nil),    // 9: ctl.StorageScanResp
	(*StorageFormatResp)(nil),  // 10: ctl.StorageFormatResp
	(*NvmeRebindResp)(nil),     // 11: ctl.NvmeRebindResp
	(*NetworkScanResp)(nil),    // 12: ctl.NetworkScanResp
	(*FirmwareQueryResp)(nil),  // 13: ctl.FirmwareQueryResp
	(*FirmwareUpdateResp)(nil), // 14: ctl.FirmwareUpdateResp
	(*SmdQueryResp)(nil),       // 15: ctl.SmdQueryResp
	(*SetLogMasksResp)(nil),    // 16: ctl.SetLogMasksResp
	(*RanksResp)(nil),          // 17: ctl.RanksResp
}
var file_ctl_ctl_proto_depIdxs = []int32{
	0,  // 0: ctl.CtlSvc.StorageScan:input_type -> ctl.StorageScanReq
	1,  // 1: ctl.CtlSvc.StorageFormat:input_type -> ctl.StorageFormatReq
	2,  // 2: ctl.CtlSvc.StorageNvmeRebind:input_type -> ctl.NvmeRebindReq
	3,  // 3: ctl.CtlSvc.NetworkScan:input_type -> ctl.NetworkScanReq
	4,  // 4: ctl.CtlSvc.FirmwareQuery:input_type -> ctl.FirmwareQueryReq
	5,  // 5: ctl.CtlSvc.FirmwareUpdate:input_type -> ctl.FirmwareUpdateReq
	6,  // 6: ctl.CtlSvc.SmdQuery:input_type -> ctl.SmdQueryReq
	7,  // 7: ctl.CtlSvc.SetEngineLogMasks:input_type -> ctl.SetLogMasksReq
	8,  // 8: ctl.CtlSvc.PrepShutdownRanks:input_type -> ctl.RanksReq
	8,  // 9: ctl.CtlSvc.StopRanks:input_type -> ctl.RanksReq
	8,  // 10: ctl.CtlSvc.PingRanks:input_type -> ctl.RanksReq
	8,  // 11: ctl.CtlSvc.ResetFormatRanks:input_type -> ctl.RanksReq
	8,  // 12: ctl.CtlSvc.StartRanks:input_type -> ctl.RanksReq
	9,  // 13: ctl.CtlSvc.StorageScan:output_type -> ctl.StorageScanResp
	10, // 14: ctl.CtlSvc.StorageFormat:output_type -> ctl.StorageFormatResp
	11, // 15: ctl.CtlSvc.StorageNvmeRebind:output_type -> ctl.NvmeRebindResp
	12, // 16: ctl.CtlSvc.NetworkScan:output_type -> ctl.NetworkScanResp
	13, // 17: ctl.CtlSvc.FirmwareQuery:output_type -> ctl.FirmwareQueryResp
	14, // 18: ctl.CtlSvc.FirmwareUpdate:output_type -> ctl.FirmwareUpdateResp
	15, // 19: ctl.CtlSvc.SmdQuery:output_type -> ctl.SmdQueryResp
	16, // 20: ctl.CtlSvc.SetEngineLogMasks:output_type -> ctl.SetLogMasksResp
	17, // 21: ctl.CtlSvc.PrepShutdownRanks:output_type -> ctl.RanksResp
	17, // 22: ctl.CtlSvc.StopRanks:output_type -> ctl.RanksResp
	17, // 23: ctl.CtlSvc.PingRanks:output_type -> ctl.RanksResp
	17, // 24: ctl.CtlSvc.ResetFormatRanks:output_type -> ctl.RanksResp
	17, // 25: ctl.CtlSvc.StartRanks:output_type -> ctl.RanksResp
	13, // [13:26] is the sub-list for method output_type
	0,  // [0:13] is the sub-list for method input_type
	0,  // [0:0] is the sub-list for extension type_name
	0,  // [0:0] is the sub-list for extension extendee
	0,  // [0:0] is the sub-list for field type_name
}

func init() { file_ctl_ctl_proto_init() }
func file_ctl_ctl_proto_init() {
	if File_ctl_ctl_proto != nil {
		return
	}
	file_ctl_storage_proto_init()
	file_ctl_network_proto_init()
	file_ctl_firmware_proto_init()
	file_ctl_smd_proto_init()
	file_ctl_ranks_proto_init()
	file_ctl_server_proto_init()
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_ctl_ctl_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   0,
			NumExtensions: 0,
			NumServices:   1,
		},
		GoTypes:           file_ctl_ctl_proto_goTypes,
		DependencyIndexes: file_ctl_ctl_proto_depIdxs,
	}.Build()
	File_ctl_ctl_proto = out.File
	file_ctl_ctl_proto_rawDesc = nil
	file_ctl_ctl_proto_goTypes = nil
	file_ctl_ctl_proto_depIdxs = nil
}
