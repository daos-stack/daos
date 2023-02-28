//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.28.0
// 	protoc        v3.5.0
// source: ctl/network.proto

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

type NetworkScanReq struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Provider          string `protobuf:"bytes,1,opt,name=provider,proto3" json:"provider,omitempty"`
	Excludeinterfaces string `protobuf:"bytes,2,opt,name=excludeinterfaces,proto3" json:"excludeinterfaces,omitempty"`
}

func (x *NetworkScanReq) Reset() {
	*x = NetworkScanReq{}
	if protoimpl.UnsafeEnabled {
		mi := &file_ctl_network_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *NetworkScanReq) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*NetworkScanReq) ProtoMessage() {}

func (x *NetworkScanReq) ProtoReflect() protoreflect.Message {
	mi := &file_ctl_network_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use NetworkScanReq.ProtoReflect.Descriptor instead.
func (*NetworkScanReq) Descriptor() ([]byte, []int) {
	return file_ctl_network_proto_rawDescGZIP(), []int{0}
}

func (x *NetworkScanReq) GetProvider() string {
	if x != nil {
		return x.Provider
	}
	return ""
}

func (x *NetworkScanReq) GetExcludeinterfaces() string {
	if x != nil {
		return x.Excludeinterfaces
	}
	return ""
}

type NetworkScanResp struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Interfaces   []*FabricInterface `protobuf:"bytes,1,rep,name=interfaces,proto3" json:"interfaces,omitempty"`
	Numacount    int32              `protobuf:"varint,2,opt,name=numacount,proto3" json:"numacount,omitempty"`
	Corespernuma int32              `protobuf:"varint,3,opt,name=corespernuma,proto3" json:"corespernuma,omitempty"` // physical cores per numa node
}

func (x *NetworkScanResp) Reset() {
	*x = NetworkScanResp{}
	if protoimpl.UnsafeEnabled {
		mi := &file_ctl_network_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *NetworkScanResp) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*NetworkScanResp) ProtoMessage() {}

func (x *NetworkScanResp) ProtoReflect() protoreflect.Message {
	mi := &file_ctl_network_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use NetworkScanResp.ProtoReflect.Descriptor instead.
func (*NetworkScanResp) Descriptor() ([]byte, []int) {
	return file_ctl_network_proto_rawDescGZIP(), []int{1}
}

func (x *NetworkScanResp) GetInterfaces() []*FabricInterface {
	if x != nil {
		return x.Interfaces
	}
	return nil
}

func (x *NetworkScanResp) GetNumacount() int32 {
	if x != nil {
		return x.Numacount
	}
	return 0
}

func (x *NetworkScanResp) GetCorespernuma() int32 {
	if x != nil {
		return x.Corespernuma
	}
	return 0
}

type FabricInterface struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Provider    string `protobuf:"bytes,1,opt,name=provider,proto3" json:"provider,omitempty"`
	Device      string `protobuf:"bytes,2,opt,name=device,proto3" json:"device,omitempty"`
	Numanode    uint32 `protobuf:"varint,3,opt,name=numanode,proto3" json:"numanode,omitempty"`
	Priority    uint32 `protobuf:"varint,4,opt,name=priority,proto3" json:"priority,omitempty"`
	Netdevclass uint32 `protobuf:"varint,5,opt,name=netdevclass,proto3" json:"netdevclass,omitempty"`
}

func (x *FabricInterface) Reset() {
	*x = FabricInterface{}
	if protoimpl.UnsafeEnabled {
		mi := &file_ctl_network_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *FabricInterface) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*FabricInterface) ProtoMessage() {}

func (x *FabricInterface) ProtoReflect() protoreflect.Message {
	mi := &file_ctl_network_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use FabricInterface.ProtoReflect.Descriptor instead.
func (*FabricInterface) Descriptor() ([]byte, []int) {
	return file_ctl_network_proto_rawDescGZIP(), []int{2}
}

func (x *FabricInterface) GetProvider() string {
	if x != nil {
		return x.Provider
	}
	return ""
}

func (x *FabricInterface) GetDevice() string {
	if x != nil {
		return x.Device
	}
	return ""
}

func (x *FabricInterface) GetNumanode() uint32 {
	if x != nil {
		return x.Numanode
	}
	return 0
}

func (x *FabricInterface) GetPriority() uint32 {
	if x != nil {
		return x.Priority
	}
	return 0
}

func (x *FabricInterface) GetNetdevclass() uint32 {
	if x != nil {
		return x.Netdevclass
	}
	return 0
}

var File_ctl_network_proto protoreflect.FileDescriptor

var file_ctl_network_proto_rawDesc = []byte{
	0x0a, 0x11, 0x63, 0x74, 0x6c, 0x2f, 0x6e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x2e, 0x70, 0x72,
	0x6f, 0x74, 0x6f, 0x12, 0x03, 0x63, 0x74, 0x6c, 0x22, 0x5a, 0x0a, 0x0e, 0x4e, 0x65, 0x74, 0x77,
	0x6f, 0x72, 0x6b, 0x53, 0x63, 0x61, 0x6e, 0x52, 0x65, 0x71, 0x12, 0x1a, 0x0a, 0x08, 0x70, 0x72,
	0x6f, 0x76, 0x69, 0x64, 0x65, 0x72, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x08, 0x70, 0x72,
	0x6f, 0x76, 0x69, 0x64, 0x65, 0x72, 0x12, 0x2c, 0x0a, 0x11, 0x65, 0x78, 0x63, 0x6c, 0x75, 0x64,
	0x65, 0x69, 0x6e, 0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x65, 0x73, 0x18, 0x02, 0x20, 0x01, 0x28,
	0x09, 0x52, 0x11, 0x65, 0x78, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x69, 0x6e, 0x74, 0x65, 0x72, 0x66,
	0x61, 0x63, 0x65, 0x73, 0x22, 0x89, 0x01, 0x0a, 0x0f, 0x4e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b,
	0x53, 0x63, 0x61, 0x6e, 0x52, 0x65, 0x73, 0x70, 0x12, 0x34, 0x0a, 0x0a, 0x69, 0x6e, 0x74, 0x65,
	0x72, 0x66, 0x61, 0x63, 0x65, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x14, 0x2e, 0x63,
	0x74, 0x6c, 0x2e, 0x46, 0x61, 0x62, 0x72, 0x69, 0x63, 0x49, 0x6e, 0x74, 0x65, 0x72, 0x66, 0x61,
	0x63, 0x65, 0x52, 0x0a, 0x69, 0x6e, 0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x65, 0x73, 0x12, 0x1c,
	0x0a, 0x09, 0x6e, 0x75, 0x6d, 0x61, 0x63, 0x6f, 0x75, 0x6e, 0x74, 0x18, 0x02, 0x20, 0x01, 0x28,
	0x05, 0x52, 0x09, 0x6e, 0x75, 0x6d, 0x61, 0x63, 0x6f, 0x75, 0x6e, 0x74, 0x12, 0x22, 0x0a, 0x0c,
	0x63, 0x6f, 0x72, 0x65, 0x73, 0x70, 0x65, 0x72, 0x6e, 0x75, 0x6d, 0x61, 0x18, 0x03, 0x20, 0x01,
	0x28, 0x05, 0x52, 0x0c, 0x63, 0x6f, 0x72, 0x65, 0x73, 0x70, 0x65, 0x72, 0x6e, 0x75, 0x6d, 0x61,
	0x22, 0x9f, 0x01, 0x0a, 0x0f, 0x46, 0x61, 0x62, 0x72, 0x69, 0x63, 0x49, 0x6e, 0x74, 0x65, 0x72,
	0x66, 0x61, 0x63, 0x65, 0x12, 0x1a, 0x0a, 0x08, 0x70, 0x72, 0x6f, 0x76, 0x69, 0x64, 0x65, 0x72,
	0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x08, 0x70, 0x72, 0x6f, 0x76, 0x69, 0x64, 0x65, 0x72,
	0x12, 0x16, 0x0a, 0x06, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09,
	0x52, 0x06, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x12, 0x1a, 0x0a, 0x08, 0x6e, 0x75, 0x6d, 0x61,
	0x6e, 0x6f, 0x64, 0x65, 0x18, 0x03, 0x20, 0x01, 0x28, 0x0d, 0x52, 0x08, 0x6e, 0x75, 0x6d, 0x61,
	0x6e, 0x6f, 0x64, 0x65, 0x12, 0x1a, 0x0a, 0x08, 0x70, 0x72, 0x69, 0x6f, 0x72, 0x69, 0x74, 0x79,
	0x18, 0x04, 0x20, 0x01, 0x28, 0x0d, 0x52, 0x08, 0x70, 0x72, 0x69, 0x6f, 0x72, 0x69, 0x74, 0x79,
	0x12, 0x20, 0x0a, 0x0b, 0x6e, 0x65, 0x74, 0x64, 0x65, 0x76, 0x63, 0x6c, 0x61, 0x73, 0x73, 0x18,
	0x05, 0x20, 0x01, 0x28, 0x0d, 0x52, 0x0b, 0x6e, 0x65, 0x74, 0x64, 0x65, 0x76, 0x63, 0x6c, 0x61,
	0x73, 0x73, 0x42, 0x39, 0x5a, 0x37, 0x67, 0x69, 0x74, 0x68, 0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d,
	0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2d, 0x73, 0x74, 0x61, 0x63, 0x6b, 0x2f, 0x64, 0x61, 0x6f, 0x73,
	0x2f, 0x73, 0x72, 0x63, 0x2f, 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x2f, 0x63, 0x6f, 0x6d,
	0x6d, 0x6f, 0x6e, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63, 0x74, 0x6c, 0x62, 0x06, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var (
	file_ctl_network_proto_rawDescOnce sync.Once
	file_ctl_network_proto_rawDescData = file_ctl_network_proto_rawDesc
)

func file_ctl_network_proto_rawDescGZIP() []byte {
	file_ctl_network_proto_rawDescOnce.Do(func() {
		file_ctl_network_proto_rawDescData = protoimpl.X.CompressGZIP(file_ctl_network_proto_rawDescData)
	})
	return file_ctl_network_proto_rawDescData
}

var file_ctl_network_proto_msgTypes = make([]protoimpl.MessageInfo, 3)
var file_ctl_network_proto_goTypes = []interface{}{
	(*NetworkScanReq)(nil),  // 0: ctl.NetworkScanReq
	(*NetworkScanResp)(nil), // 1: ctl.NetworkScanResp
	(*FabricInterface)(nil), // 2: ctl.FabricInterface
}
var file_ctl_network_proto_depIdxs = []int32{
	2, // 0: ctl.NetworkScanResp.interfaces:type_name -> ctl.FabricInterface
	1, // [1:1] is the sub-list for method output_type
	1, // [1:1] is the sub-list for method input_type
	1, // [1:1] is the sub-list for extension type_name
	1, // [1:1] is the sub-list for extension extendee
	0, // [0:1] is the sub-list for field type_name
}

func init() { file_ctl_network_proto_init() }
func file_ctl_network_proto_init() {
	if File_ctl_network_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_ctl_network_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*NetworkScanReq); i {
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
		file_ctl_network_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*NetworkScanResp); i {
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
		file_ctl_network_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*FabricInterface); i {
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
			RawDescriptor: file_ctl_network_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   3,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_ctl_network_proto_goTypes,
		DependencyIndexes: file_ctl_network_proto_depIdxs,
		MessageInfos:      file_ctl_network_proto_msgTypes,
	}.Build()
	File_ctl_network_proto = out.File
	file_ctl_network_proto_rawDesc = nil
	file_ctl_network_proto_goTypes = nil
	file_ctl_network_proto_depIdxs = nil
}
