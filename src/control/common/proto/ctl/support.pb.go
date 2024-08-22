//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.30.0
// 	protoc        v3.5.0
// source: ctl/support.proto

package ctl

import (
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type CollectLogReq struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	TargetFolder         string `protobuf:"bytes,1,opt,name=TargetFolder,proto3" json:"TargetFolder,omitempty"`
	ExtraLogsDir         string `protobuf:"bytes,2,opt,name=ExtraLogsDir,proto3" json:"ExtraLogsDir,omitempty"`
	AdminNode            string `protobuf:"bytes,3,opt,name=AdminNode,proto3" json:"AdminNode,omitempty"`
	JsonOutput           bool   `protobuf:"varint,4,opt,name=JsonOutput,proto3" json:"JsonOutput,omitempty"`
	LogFunction          int32  `protobuf:"varint,5,opt,name=LogFunction,proto3" json:"LogFunction,omitempty"`
	LogCmd               string `protobuf:"bytes,6,opt,name=LogCmd,proto3" json:"LogCmd,omitempty"`
	LogStartDate         string `protobuf:"bytes,7,opt,name=LogStartDate,proto3" json:"LogStartDate,omitempty"`
	LogEndDate           string `protobuf:"bytes,8,opt,name=LogEndDate,proto3" json:"LogEndDate,omitempty"`
	LogStartTime         string `protobuf:"bytes,9,opt,name=LogStartTime,proto3" json:"LogStartTime,omitempty"`
	LogEndTime           string `protobuf:"bytes,10,opt,name=LogEndTime,proto3" json:"LogEndTime,omitempty"`
	StopOnError          bool   `protobuf:"varint,11,opt,name=StopOnError,proto3" json:"StopOnError,omitempty"`
	FileTransferExecArgs string `protobuf:"bytes,12,opt,name=FileTransferExecArgs,proto3" json:"FileTransferExecArgs,omitempty"`
}

func (x *CollectLogReq) Reset() {
	*x = CollectLogReq{}
	if protoimpl.UnsafeEnabled {
		mi := &file_ctl_support_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *CollectLogReq) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*CollectLogReq) ProtoMessage() {}

func (x *CollectLogReq) ProtoReflect() protoreflect.Message {
	mi := &file_ctl_support_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use CollectLogReq.ProtoReflect.Descriptor instead.
func (*CollectLogReq) Descriptor() ([]byte, []int) {
	return file_ctl_support_proto_rawDescGZIP(), []int{0}
}

func (x *CollectLogReq) GetTargetFolder() string {
	if x != nil {
		return x.TargetFolder
	}
	return ""
}

func (x *CollectLogReq) GetExtraLogsDir() string {
	if x != nil {
		return x.ExtraLogsDir
	}
	return ""
}

func (x *CollectLogReq) GetAdminNode() string {
	if x != nil {
		return x.AdminNode
	}
	return ""
}

func (x *CollectLogReq) GetJsonOutput() bool {
	if x != nil {
		return x.JsonOutput
	}
	return false
}

func (x *CollectLogReq) GetLogFunction() int32 {
	if x != nil {
		return x.LogFunction
	}
	return 0
}

func (x *CollectLogReq) GetLogCmd() string {
	if x != nil {
		return x.LogCmd
	}
	return ""
}

func (x *CollectLogReq) GetLogStartDate() string {
	if x != nil {
		return x.LogStartDate
	}
	return ""
}

func (x *CollectLogReq) GetLogEndDate() string {
	if x != nil {
		return x.LogEndDate
	}
	return ""
}

func (x *CollectLogReq) GetLogStartTime() string {
	if x != nil {
		return x.LogStartTime
	}
	return ""
}

func (x *CollectLogReq) GetLogEndTime() string {
	if x != nil {
		return x.LogEndTime
	}
	return ""
}

func (x *CollectLogReq) GetStopOnError() bool {
	if x != nil {
		return x.StopOnError
	}
	return false
}

func (x *CollectLogReq) GetFileTransferExecArgs() string {
	if x != nil {
		return x.FileTransferExecArgs
	}
	return ""
}

type CollectLogResp struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Status int32 `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"` // DAOS error code
}

func (x *CollectLogResp) Reset() {
	*x = CollectLogResp{}
	if protoimpl.UnsafeEnabled {
		mi := &file_ctl_support_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *CollectLogResp) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*CollectLogResp) ProtoMessage() {}

func (x *CollectLogResp) ProtoReflect() protoreflect.Message {
	mi := &file_ctl_support_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use CollectLogResp.ProtoReflect.Descriptor instead.
func (*CollectLogResp) Descriptor() ([]byte, []int) {
	return file_ctl_support_proto_rawDescGZIP(), []int{1}
}

func (x *CollectLogResp) GetStatus() int32 {
	if x != nil {
		return x.Status
	}
	return 0
}

var File_ctl_support_proto protoreflect.FileDescriptor

var file_ctl_support_proto_rawDesc = []byte{
	0x0a, 0x11, 0x63, 0x74, 0x6c, 0x2f, 0x73, 0x75, 0x70, 0x70, 0x6f, 0x72, 0x74, 0x2e, 0x70, 0x72,
	0x6f, 0x74, 0x6f, 0x12, 0x03, 0x63, 0x74, 0x6c, 0x22, 0xad, 0x03, 0x0a, 0x0d, 0x43, 0x6f, 0x6c,
	0x6c, 0x65, 0x63, 0x74, 0x4c, 0x6f, 0x67, 0x52, 0x65, 0x71, 0x12, 0x22, 0x0a, 0x0c, 0x54, 0x61,
	0x72, 0x67, 0x65, 0x74, 0x46, 0x6f, 0x6c, 0x64, 0x65, 0x72, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09,
	0x52, 0x0c, 0x54, 0x61, 0x72, 0x67, 0x65, 0x74, 0x46, 0x6f, 0x6c, 0x64, 0x65, 0x72, 0x12, 0x22,
	0x0a, 0x0c, 0x45, 0x78, 0x74, 0x72, 0x61, 0x4c, 0x6f, 0x67, 0x73, 0x44, 0x69, 0x72, 0x18, 0x02,
	0x20, 0x01, 0x28, 0x09, 0x52, 0x0c, 0x45, 0x78, 0x74, 0x72, 0x61, 0x4c, 0x6f, 0x67, 0x73, 0x44,
	0x69, 0x72, 0x12, 0x1c, 0x0a, 0x09, 0x41, 0x64, 0x6d, 0x69, 0x6e, 0x4e, 0x6f, 0x64, 0x65, 0x18,
	0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x09, 0x41, 0x64, 0x6d, 0x69, 0x6e, 0x4e, 0x6f, 0x64, 0x65,
	0x12, 0x1e, 0x0a, 0x0a, 0x4a, 0x73, 0x6f, 0x6e, 0x4f, 0x75, 0x74, 0x70, 0x75, 0x74, 0x18, 0x04,
	0x20, 0x01, 0x28, 0x08, 0x52, 0x0a, 0x4a, 0x73, 0x6f, 0x6e, 0x4f, 0x75, 0x74, 0x70, 0x75, 0x74,
	0x12, 0x20, 0x0a, 0x0b, 0x4c, 0x6f, 0x67, 0x46, 0x75, 0x6e, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x18,
	0x05, 0x20, 0x01, 0x28, 0x05, 0x52, 0x0b, 0x4c, 0x6f, 0x67, 0x46, 0x75, 0x6e, 0x63, 0x74, 0x69,
	0x6f, 0x6e, 0x12, 0x16, 0x0a, 0x06, 0x4c, 0x6f, 0x67, 0x43, 0x6d, 0x64, 0x18, 0x06, 0x20, 0x01,
	0x28, 0x09, 0x52, 0x06, 0x4c, 0x6f, 0x67, 0x43, 0x6d, 0x64, 0x12, 0x22, 0x0a, 0x0c, 0x4c, 0x6f,
	0x67, 0x53, 0x74, 0x61, 0x72, 0x74, 0x44, 0x61, 0x74, 0x65, 0x18, 0x07, 0x20, 0x01, 0x28, 0x09,
	0x52, 0x0c, 0x4c, 0x6f, 0x67, 0x53, 0x74, 0x61, 0x72, 0x74, 0x44, 0x61, 0x74, 0x65, 0x12, 0x1e,
	0x0a, 0x0a, 0x4c, 0x6f, 0x67, 0x45, 0x6e, 0x64, 0x44, 0x61, 0x74, 0x65, 0x18, 0x08, 0x20, 0x01,
	0x28, 0x09, 0x52, 0x0a, 0x4c, 0x6f, 0x67, 0x45, 0x6e, 0x64, 0x44, 0x61, 0x74, 0x65, 0x12, 0x22,
	0x0a, 0x0c, 0x4c, 0x6f, 0x67, 0x53, 0x74, 0x61, 0x72, 0x74, 0x54, 0x69, 0x6d, 0x65, 0x18, 0x09,
	0x20, 0x01, 0x28, 0x09, 0x52, 0x0c, 0x4c, 0x6f, 0x67, 0x53, 0x74, 0x61, 0x72, 0x74, 0x54, 0x69,
	0x6d, 0x65, 0x12, 0x1e, 0x0a, 0x0a, 0x4c, 0x6f, 0x67, 0x45, 0x6e, 0x64, 0x54, 0x69, 0x6d, 0x65,
	0x18, 0x0a, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0a, 0x4c, 0x6f, 0x67, 0x45, 0x6e, 0x64, 0x54, 0x69,
	0x6d, 0x65, 0x12, 0x20, 0x0a, 0x0b, 0x53, 0x74, 0x6f, 0x70, 0x4f, 0x6e, 0x45, 0x72, 0x72, 0x6f,
	0x72, 0x18, 0x0b, 0x20, 0x01, 0x28, 0x08, 0x52, 0x0b, 0x53, 0x74, 0x6f, 0x70, 0x4f, 0x6e, 0x45,
	0x72, 0x72, 0x6f, 0x72, 0x12, 0x32, 0x0a, 0x14, 0x46, 0x69, 0x6c, 0x65, 0x54, 0x72, 0x61, 0x6e,
	0x73, 0x66, 0x65, 0x72, 0x45, 0x78, 0x65, 0x63, 0x41, 0x72, 0x67, 0x73, 0x18, 0x0c, 0x20, 0x01,
	0x28, 0x09, 0x52, 0x14, 0x46, 0x69, 0x6c, 0x65, 0x54, 0x72, 0x61, 0x6e, 0x73, 0x66, 0x65, 0x72,
	0x45, 0x78, 0x65, 0x63, 0x41, 0x72, 0x67, 0x73, 0x22, 0x28, 0x0a, 0x0e, 0x43, 0x6f, 0x6c, 0x6c,
	0x65, 0x63, 0x74, 0x4c, 0x6f, 0x67, 0x52, 0x65, 0x73, 0x70, 0x12, 0x16, 0x0a, 0x06, 0x73, 0x74,
	0x61, 0x74, 0x75, 0x73, 0x18, 0x01, 0x20, 0x01, 0x28, 0x05, 0x52, 0x06, 0x73, 0x74, 0x61, 0x74,
	0x75, 0x73, 0x42, 0x39, 0x5a, 0x37, 0x67, 0x69, 0x74, 0x68, 0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d,
	0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2d, 0x73, 0x74, 0x61, 0x63, 0x6b, 0x2f, 0x64, 0x61, 0x6f, 0x73,
	0x2f, 0x73, 0x72, 0x63, 0x2f, 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x2f, 0x63, 0x6f, 0x6d,
	0x6d, 0x6f, 0x6e, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63, 0x74, 0x6c, 0x62, 0x06, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var (
	file_ctl_support_proto_rawDescOnce sync.Once
	file_ctl_support_proto_rawDescData = file_ctl_support_proto_rawDesc
)

func file_ctl_support_proto_rawDescGZIP() []byte {
	file_ctl_support_proto_rawDescOnce.Do(func() {
		file_ctl_support_proto_rawDescData = protoimpl.X.CompressGZIP(file_ctl_support_proto_rawDescData)
	})
	return file_ctl_support_proto_rawDescData
}

var file_ctl_support_proto_msgTypes = make([]protoimpl.MessageInfo, 2)
var file_ctl_support_proto_goTypes = []interface{}{
	(*CollectLogReq)(nil),  // 0: ctl.CollectLogReq
	(*CollectLogResp)(nil), // 1: ctl.CollectLogResp
}
var file_ctl_support_proto_depIdxs = []int32{
	0, // [0:0] is the sub-list for method output_type
	0, // [0:0] is the sub-list for method input_type
	0, // [0:0] is the sub-list for extension type_name
	0, // [0:0] is the sub-list for extension extendee
	0, // [0:0] is the sub-list for field type_name
}

func init() { file_ctl_support_proto_init() }
func file_ctl_support_proto_init() {
	if File_ctl_support_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_ctl_support_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*CollectLogReq); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_ctl_support_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*CollectLogResp); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_ctl_support_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   2,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_ctl_support_proto_goTypes,
		DependencyIndexes: file_ctl_support_proto_depIdxs,
		MessageInfos:      file_ctl_support_proto_msgTypes,
	}.Build()
	File_ctl_support_proto = out.File
	file_ctl_support_proto_rawDesc = nil
	file_ctl_support_proto_goTypes = nil
	file_ctl_support_proto_depIdxs = nil
}
