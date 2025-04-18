//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.34.1
// 	protoc        v3.5.0
// source: mgmt/cont.proto

package mgmt

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

// ContSetOwnerReq changes the ownership of a container.
type ContSetOwnerReq struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Sys        string   `protobuf:"bytes,1,opt,name=sys,proto3" json:"sys,omitempty"`                                   // DAOS system identifier
	ContId     string   `protobuf:"bytes,2,opt,name=cont_id,json=contId,proto3" json:"cont_id,omitempty"`               // UUID or label of the container
	PoolId     string   `protobuf:"bytes,3,opt,name=pool_id,json=poolId,proto3" json:"pool_id,omitempty"`               // UUID or label of the pool that the container is in
	OwnerUser  string   `protobuf:"bytes,4,opt,name=owner_user,json=ownerUser,proto3" json:"owner_user,omitempty"`      // formatted user e.g. "bob@"
	OwnerGroup string   `protobuf:"bytes,5,opt,name=owner_group,json=ownerGroup,proto3" json:"owner_group,omitempty"`   // formatted group e.g. "builders@"
	SvcRanks   []uint32 `protobuf:"varint,6,rep,packed,name=svc_ranks,json=svcRanks,proto3" json:"svc_ranks,omitempty"` // List of pool service ranks
}

func (x *ContSetOwnerReq) Reset() {
	*x = ContSetOwnerReq{}
	if protoimpl.UnsafeEnabled {
		mi := &file_mgmt_cont_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *ContSetOwnerReq) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*ContSetOwnerReq) ProtoMessage() {}

func (x *ContSetOwnerReq) ProtoReflect() protoreflect.Message {
	mi := &file_mgmt_cont_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use ContSetOwnerReq.ProtoReflect.Descriptor instead.
func (*ContSetOwnerReq) Descriptor() ([]byte, []int) {
	return file_mgmt_cont_proto_rawDescGZIP(), []int{0}
}

func (x *ContSetOwnerReq) GetSys() string {
	if x != nil {
		return x.Sys
	}
	return ""
}

func (x *ContSetOwnerReq) GetContId() string {
	if x != nil {
		return x.ContId
	}
	return ""
}

func (x *ContSetOwnerReq) GetPoolId() string {
	if x != nil {
		return x.PoolId
	}
	return ""
}

func (x *ContSetOwnerReq) GetOwnerUser() string {
	if x != nil {
		return x.OwnerUser
	}
	return ""
}

func (x *ContSetOwnerReq) GetOwnerGroup() string {
	if x != nil {
		return x.OwnerGroup
	}
	return ""
}

func (x *ContSetOwnerReq) GetSvcRanks() []uint32 {
	if x != nil {
		return x.SvcRanks
	}
	return nil
}

var File_mgmt_cont_proto protoreflect.FileDescriptor

var file_mgmt_cont_proto_rawDesc = []byte{
	0x0a, 0x0f, 0x6d, 0x67, 0x6d, 0x74, 0x2f, 0x63, 0x6f, 0x6e, 0x74, 0x2e, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x12, 0x04, 0x6d, 0x67, 0x6d, 0x74, 0x22, 0xb2, 0x01, 0x0a, 0x0f, 0x43, 0x6f, 0x6e, 0x74,
	0x53, 0x65, 0x74, 0x4f, 0x77, 0x6e, 0x65, 0x72, 0x52, 0x65, 0x71, 0x12, 0x10, 0x0a, 0x03, 0x73,
	0x79, 0x73, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x03, 0x73, 0x79, 0x73, 0x12, 0x17, 0x0a,
	0x07, 0x63, 0x6f, 0x6e, 0x74, 0x5f, 0x69, 0x64, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x06,
	0x63, 0x6f, 0x6e, 0x74, 0x49, 0x64, 0x12, 0x17, 0x0a, 0x07, 0x70, 0x6f, 0x6f, 0x6c, 0x5f, 0x69,
	0x64, 0x18, 0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x06, 0x70, 0x6f, 0x6f, 0x6c, 0x49, 0x64, 0x12,
	0x1d, 0x0a, 0x0a, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x5f, 0x75, 0x73, 0x65, 0x72, 0x18, 0x04, 0x20,
	0x01, 0x28, 0x09, 0x52, 0x09, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x55, 0x73, 0x65, 0x72, 0x12, 0x1f,
	0x0a, 0x0b, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x5f, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x18, 0x05, 0x20,
	0x01, 0x28, 0x09, 0x52, 0x0a, 0x6f, 0x77, 0x6e, 0x65, 0x72, 0x47, 0x72, 0x6f, 0x75, 0x70, 0x12,
	0x1b, 0x0a, 0x09, 0x73, 0x76, 0x63, 0x5f, 0x72, 0x61, 0x6e, 0x6b, 0x73, 0x18, 0x06, 0x20, 0x03,
	0x28, 0x0d, 0x52, 0x08, 0x73, 0x76, 0x63, 0x52, 0x61, 0x6e, 0x6b, 0x73, 0x42, 0x3a, 0x5a, 0x38,
	0x67, 0x69, 0x74, 0x68, 0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2d,
	0x73, 0x74, 0x61, 0x63, 0x6b, 0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2f, 0x73, 0x72, 0x63, 0x2f, 0x63,
	0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x2f, 0x63, 0x6f, 0x6d, 0x6d, 0x6f, 0x6e, 0x2f, 0x70, 0x72,
	0x6f, 0x74, 0x6f, 0x2f, 0x6d, 0x67, 0x6d, 0x74, 0x62, 0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var (
	file_mgmt_cont_proto_rawDescOnce sync.Once
	file_mgmt_cont_proto_rawDescData = file_mgmt_cont_proto_rawDesc
)

func file_mgmt_cont_proto_rawDescGZIP() []byte {
	file_mgmt_cont_proto_rawDescOnce.Do(func() {
		file_mgmt_cont_proto_rawDescData = protoimpl.X.CompressGZIP(file_mgmt_cont_proto_rawDescData)
	})
	return file_mgmt_cont_proto_rawDescData
}

var file_mgmt_cont_proto_msgTypes = make([]protoimpl.MessageInfo, 1)
var file_mgmt_cont_proto_goTypes = []interface{}{
	(*ContSetOwnerReq)(nil), // 0: mgmt.ContSetOwnerReq
}
var file_mgmt_cont_proto_depIdxs = []int32{
	0, // [0:0] is the sub-list for method output_type
	0, // [0:0] is the sub-list for method input_type
	0, // [0:0] is the sub-list for extension type_name
	0, // [0:0] is the sub-list for extension extendee
	0, // [0:0] is the sub-list for field type_name
}

func init() { file_mgmt_cont_proto_init() }
func file_mgmt_cont_proto_init() {
	if File_mgmt_cont_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_mgmt_cont_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*ContSetOwnerReq); i {
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
			RawDescriptor: file_mgmt_cont_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   1,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_mgmt_cont_proto_goTypes,
		DependencyIndexes: file_mgmt_cont_proto_depIdxs,
		MessageInfos:      file_mgmt_cont_proto_msgTypes,
	}.Build()
	File_mgmt_cont_proto = out.File
	file_mgmt_cont_proto_rawDesc = nil
	file_mgmt_cont_proto_goTypes = nil
	file_mgmt_cont_proto_depIdxs = nil
}
