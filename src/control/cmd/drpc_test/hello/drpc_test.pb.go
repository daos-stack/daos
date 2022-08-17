//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.6.1
// source: drpc_test.proto

package hello

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

type Module int32

const (
	Module_HELLO Module = 0
)

// Enum value maps for Module.
var (
	Module_name = map[int32]string{
		0: "HELLO",
	}
	Module_value = map[string]int32{
		"HELLO": 0,
	}
)

func (x Module) Enum() *Module {
	p := new(Module)
	*p = x
	return p
}

func (x Module) String() string {
	return protoimpl.X.EnumStringOf(x.Descriptor(), protoreflect.EnumNumber(x))
}

func (Module) Descriptor() protoreflect.EnumDescriptor {
	return file_drpc_test_proto_enumTypes[0].Descriptor()
}

func (Module) Type() protoreflect.EnumType {
	return &file_drpc_test_proto_enumTypes[0]
}

func (x Module) Number() protoreflect.EnumNumber {
	return protoreflect.EnumNumber(x)
}

// Deprecated: Use Module.Descriptor instead.
func (Module) EnumDescriptor() ([]byte, []int) {
	return file_drpc_test_proto_rawDescGZIP(), []int{0}
}

type Function int32

const (
	Function_GREETING Function = 0
)

// Enum value maps for Function.
var (
	Function_name = map[int32]string{
		0: "GREETING",
	}
	Function_value = map[string]int32{
		"GREETING": 0,
	}
)

func (x Function) Enum() *Function {
	p := new(Function)
	*p = x
	return p
}

func (x Function) String() string {
	return protoimpl.X.EnumStringOf(x.Descriptor(), protoreflect.EnumNumber(x))
}

func (Function) Descriptor() protoreflect.EnumDescriptor {
	return file_drpc_test_proto_enumTypes[1].Descriptor()
}

func (Function) Type() protoreflect.EnumType {
	return &file_drpc_test_proto_enumTypes[1]
}

func (x Function) Number() protoreflect.EnumNumber {
	return protoreflect.EnumNumber(x)
}

// Deprecated: Use Function.Descriptor instead.
func (Function) EnumDescriptor() ([]byte, []int) {
	return file_drpc_test_proto_rawDescGZIP(), []int{1}
}

// *
// Hello is the message to request a greeting from the server
//
// name is the name of the user being greeted
type Hello struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Name string `protobuf:"bytes,1,opt,name=name,proto3" json:"name,omitempty"`
}

func (x *Hello) Reset() {
	*x = Hello{}
	if protoimpl.UnsafeEnabled {
		mi := &file_drpc_test_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *Hello) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*Hello) ProtoMessage() {}

func (x *Hello) ProtoReflect() protoreflect.Message {
	mi := &file_drpc_test_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use Hello.ProtoReflect.Descriptor instead.
func (*Hello) Descriptor() ([]byte, []int) {
	return file_drpc_test_proto_rawDescGZIP(), []int{0}
}

func (x *Hello) GetName() string {
	if x != nil {
		return x.Name
	}
	return ""
}

// *
// HeloResponse is the greeting returned from the server.
//
// greeting is greeting message for the user.
type HelloResponse struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Greeting string `protobuf:"bytes,1,opt,name=greeting,proto3" json:"greeting,omitempty"`
}

func (x *HelloResponse) Reset() {
	*x = HelloResponse{}
	if protoimpl.UnsafeEnabled {
		mi := &file_drpc_test_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *HelloResponse) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*HelloResponse) ProtoMessage() {}

func (x *HelloResponse) ProtoReflect() protoreflect.Message {
	mi := &file_drpc_test_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use HelloResponse.ProtoReflect.Descriptor instead.
func (*HelloResponse) Descriptor() ([]byte, []int) {
	return file_drpc_test_proto_rawDescGZIP(), []int{1}
}

func (x *HelloResponse) GetGreeting() string {
	if x != nil {
		return x.Greeting
	}
	return ""
}

var File_drpc_test_proto protoreflect.FileDescriptor

var file_drpc_test_proto_rawDesc = []byte{
	0x0a, 0x0f, 0x64, 0x72, 0x70, 0x63, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x12, 0x05, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x22, 0x1b, 0x0a, 0x05, 0x48, 0x65, 0x6c, 0x6c,
	0x6f, 0x12, 0x12, 0x0a, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52,
	0x04, 0x6e, 0x61, 0x6d, 0x65, 0x22, 0x2b, 0x0a, 0x0d, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x52, 0x65,
	0x73, 0x70, 0x6f, 0x6e, 0x73, 0x65, 0x12, 0x1a, 0x0a, 0x08, 0x67, 0x72, 0x65, 0x65, 0x74, 0x69,
	0x6e, 0x67, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x08, 0x67, 0x72, 0x65, 0x65, 0x74, 0x69,
	0x6e, 0x67, 0x2a, 0x13, 0x0a, 0x06, 0x4d, 0x6f, 0x64, 0x75, 0x6c, 0x65, 0x12, 0x09, 0x0a, 0x05,
	0x48, 0x45, 0x4c, 0x4c, 0x4f, 0x10, 0x00, 0x2a, 0x18, 0x0a, 0x08, 0x46, 0x75, 0x6e, 0x63, 0x74,
	0x69, 0x6f, 0x6e, 0x12, 0x0c, 0x0a, 0x08, 0x47, 0x52, 0x45, 0x45, 0x54, 0x49, 0x4e, 0x47, 0x10,
	0x00, 0x42, 0x3c, 0x5a, 0x3a, 0x67, 0x69, 0x74, 0x68, 0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d, 0x2f,
	0x64, 0x61, 0x6f, 0x73, 0x2d, 0x73, 0x74, 0x61, 0x63, 0x6b, 0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2f,
	0x73, 0x72, 0x63, 0x2f, 0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x2f, 0x63, 0x6d, 0x64, 0x2f,
	0x64, 0x72, 0x70, 0x63, 0x5f, 0x74, 0x65, 0x73, 0x74, 0x2f, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x62,
	0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var (
	file_drpc_test_proto_rawDescOnce sync.Once
	file_drpc_test_proto_rawDescData = file_drpc_test_proto_rawDesc
)

func file_drpc_test_proto_rawDescGZIP() []byte {
	file_drpc_test_proto_rawDescOnce.Do(func() {
		file_drpc_test_proto_rawDescData = protoimpl.X.CompressGZIP(file_drpc_test_proto_rawDescData)
	})
	return file_drpc_test_proto_rawDescData
}

var file_drpc_test_proto_enumTypes = make([]protoimpl.EnumInfo, 2)
var file_drpc_test_proto_msgTypes = make([]protoimpl.MessageInfo, 2)
var file_drpc_test_proto_goTypes = []interface{}{
	(Module)(0),           // 0: hello.Module
	(Function)(0),         // 1: hello.Function
	(*Hello)(nil),         // 2: hello.Hello
	(*HelloResponse)(nil), // 3: hello.HelloResponse
}
var file_drpc_test_proto_depIdxs = []int32{
	0, // [0:0] is the sub-list for method output_type
	0, // [0:0] is the sub-list for method input_type
	0, // [0:0] is the sub-list for extension type_name
	0, // [0:0] is the sub-list for extension extendee
	0, // [0:0] is the sub-list for field type_name
}

func init() { file_drpc_test_proto_init() }
func file_drpc_test_proto_init() {
	if File_drpc_test_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_drpc_test_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*Hello); i {
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
		file_drpc_test_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*HelloResponse); i {
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
			RawDescriptor: file_drpc_test_proto_rawDesc,
			NumEnums:      2,
			NumMessages:   2,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_drpc_test_proto_goTypes,
		DependencyIndexes: file_drpc_test_proto_depIdxs,
		EnumInfos:         file_drpc_test_proto_enumTypes,
		MessageInfos:      file_drpc_test_proto_msgTypes,
	}.Build()
	File_drpc_test_proto = out.File
	file_drpc_test_proto_rawDesc = nil
	file_drpc_test_proto_goTypes = nil
	file_drpc_test_proto_depIdxs = nil
}
