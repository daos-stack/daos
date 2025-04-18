//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.34.1
// 	protoc        v3.5.0
// source: auth.proto

package auth

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

// Types of authentication token
type Flavor int32

const (
	Flavor_AUTH_NONE Flavor = 0
	Flavor_AUTH_SYS  Flavor = 1
)

// Enum value maps for Flavor.
var (
	Flavor_name = map[int32]string{
		0: "AUTH_NONE",
		1: "AUTH_SYS",
	}
	Flavor_value = map[string]int32{
		"AUTH_NONE": 0,
		"AUTH_SYS":  1,
	}
)

func (x Flavor) Enum() *Flavor {
	p := new(Flavor)
	*p = x
	return p
}

func (x Flavor) String() string {
	return protoimpl.X.EnumStringOf(x.Descriptor(), protoreflect.EnumNumber(x))
}

func (Flavor) Descriptor() protoreflect.EnumDescriptor {
	return file_auth_proto_enumTypes[0].Descriptor()
}

func (Flavor) Type() protoreflect.EnumType {
	return &file_auth_proto_enumTypes[0]
}

func (x Flavor) Number() protoreflect.EnumNumber {
	return protoreflect.EnumNumber(x)
}

// Deprecated: Use Flavor.Descriptor instead.
func (Flavor) EnumDescriptor() ([]byte, []int) {
	return file_auth_proto_rawDescGZIP(), []int{0}
}

type Token struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Flavor Flavor `protobuf:"varint,1,opt,name=flavor,proto3,enum=auth.Flavor" json:"flavor,omitempty"` // flavor of this authentication token
	Data   []byte `protobuf:"bytes,2,opt,name=data,proto3" json:"data,omitempty"`                       // packed structure of the specified flavor
}

func (x *Token) Reset() {
	*x = Token{}
	if protoimpl.UnsafeEnabled {
		mi := &file_auth_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *Token) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*Token) ProtoMessage() {}

func (x *Token) ProtoReflect() protoreflect.Message {
	mi := &file_auth_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use Token.ProtoReflect.Descriptor instead.
func (*Token) Descriptor() ([]byte, []int) {
	return file_auth_proto_rawDescGZIP(), []int{0}
}

func (x *Token) GetFlavor() Flavor {
	if x != nil {
		return x.Flavor
	}
	return Flavor_AUTH_NONE
}

func (x *Token) GetData() []byte {
	if x != nil {
		return x.Data
	}
	return nil
}

// Token structure for AUTH_SYS flavor cred
type Sys struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Stamp       uint64   `protobuf:"varint,1,opt,name=stamp,proto3" json:"stamp,omitempty"`            // timestamp
	Machinename string   `protobuf:"bytes,2,opt,name=machinename,proto3" json:"machinename,omitempty"` // machine name
	User        string   `protobuf:"bytes,3,opt,name=user,proto3" json:"user,omitempty"`               // user name
	Group       string   `protobuf:"bytes,4,opt,name=group,proto3" json:"group,omitempty"`             // primary group name
	Groups      []string `protobuf:"bytes,5,rep,name=groups,proto3" json:"groups,omitempty"`           // secondary group names
	Secctx      string   `protobuf:"bytes,6,opt,name=secctx,proto3" json:"secctx,omitempty"`           // Additional field for MAC label
}

func (x *Sys) Reset() {
	*x = Sys{}
	if protoimpl.UnsafeEnabled {
		mi := &file_auth_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *Sys) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*Sys) ProtoMessage() {}

func (x *Sys) ProtoReflect() protoreflect.Message {
	mi := &file_auth_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use Sys.ProtoReflect.Descriptor instead.
func (*Sys) Descriptor() ([]byte, []int) {
	return file_auth_proto_rawDescGZIP(), []int{1}
}

func (x *Sys) GetStamp() uint64 {
	if x != nil {
		return x.Stamp
	}
	return 0
}

func (x *Sys) GetMachinename() string {
	if x != nil {
		return x.Machinename
	}
	return ""
}

func (x *Sys) GetUser() string {
	if x != nil {
		return x.User
	}
	return ""
}

func (x *Sys) GetGroup() string {
	if x != nil {
		return x.Group
	}
	return ""
}

func (x *Sys) GetGroups() []string {
	if x != nil {
		return x.Groups
	}
	return nil
}

func (x *Sys) GetSecctx() string {
	if x != nil {
		return x.Secctx
	}
	return ""
}

// Token and verifier are expected to have the same flavor type.
type Credential struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Token    *Token `protobuf:"bytes,1,opt,name=token,proto3" json:"token,omitempty"`       // authentication token
	Verifier *Token `protobuf:"bytes,2,opt,name=verifier,proto3" json:"verifier,omitempty"` // to verify integrity of the token
	Origin   string `protobuf:"bytes,3,opt,name=origin,proto3" json:"origin,omitempty"`     // the agent that created this credential
}

func (x *Credential) Reset() {
	*x = Credential{}
	if protoimpl.UnsafeEnabled {
		mi := &file_auth_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *Credential) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*Credential) ProtoMessage() {}

func (x *Credential) ProtoReflect() protoreflect.Message {
	mi := &file_auth_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use Credential.ProtoReflect.Descriptor instead.
func (*Credential) Descriptor() ([]byte, []int) {
	return file_auth_proto_rawDescGZIP(), []int{2}
}

func (x *Credential) GetToken() *Token {
	if x != nil {
		return x.Token
	}
	return nil
}

func (x *Credential) GetVerifier() *Token {
	if x != nil {
		return x.Verifier
	}
	return nil
}

func (x *Credential) GetOrigin() string {
	if x != nil {
		return x.Origin
	}
	return ""
}

// GetCredResp represents the result of a request to fetch authentication
// credentials.
type GetCredResp struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Status int32       `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"` // Status of the request
	Cred   *Credential `protobuf:"bytes,2,opt,name=cred,proto3" json:"cred,omitempty"`      // Caller's authentication credential
}

func (x *GetCredResp) Reset() {
	*x = GetCredResp{}
	if protoimpl.UnsafeEnabled {
		mi := &file_auth_proto_msgTypes[3]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *GetCredResp) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*GetCredResp) ProtoMessage() {}

func (x *GetCredResp) ProtoReflect() protoreflect.Message {
	mi := &file_auth_proto_msgTypes[3]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use GetCredResp.ProtoReflect.Descriptor instead.
func (*GetCredResp) Descriptor() ([]byte, []int) {
	return file_auth_proto_rawDescGZIP(), []int{3}
}

func (x *GetCredResp) GetStatus() int32 {
	if x != nil {
		return x.Status
	}
	return 0
}

func (x *GetCredResp) GetCred() *Credential {
	if x != nil {
		return x.Cred
	}
	return nil
}

// ValidateCredReq represents a request to verify a set of authentication
// credentials.
type ValidateCredReq struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Cred *Credential `protobuf:"bytes,1,opt,name=cred,proto3" json:"cred,omitempty"` // Credential to be validated
}

func (x *ValidateCredReq) Reset() {
	*x = ValidateCredReq{}
	if protoimpl.UnsafeEnabled {
		mi := &file_auth_proto_msgTypes[4]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *ValidateCredReq) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*ValidateCredReq) ProtoMessage() {}

func (x *ValidateCredReq) ProtoReflect() protoreflect.Message {
	mi := &file_auth_proto_msgTypes[4]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use ValidateCredReq.ProtoReflect.Descriptor instead.
func (*ValidateCredReq) Descriptor() ([]byte, []int) {
	return file_auth_proto_rawDescGZIP(), []int{4}
}

func (x *ValidateCredReq) GetCred() *Credential {
	if x != nil {
		return x.Cred
	}
	return nil
}

// ValidateCredResp represents the result of a request to validate
// authentication credentials.
type ValidateCredResp struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Status int32  `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"` // Status of the request
	Token  *Token `protobuf:"bytes,2,opt,name=token,proto3" json:"token,omitempty"`    // Validated authentication token from the credential
}

func (x *ValidateCredResp) Reset() {
	*x = ValidateCredResp{}
	if protoimpl.UnsafeEnabled {
		mi := &file_auth_proto_msgTypes[5]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *ValidateCredResp) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*ValidateCredResp) ProtoMessage() {}

func (x *ValidateCredResp) ProtoReflect() protoreflect.Message {
	mi := &file_auth_proto_msgTypes[5]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use ValidateCredResp.ProtoReflect.Descriptor instead.
func (*ValidateCredResp) Descriptor() ([]byte, []int) {
	return file_auth_proto_rawDescGZIP(), []int{5}
}

func (x *ValidateCredResp) GetStatus() int32 {
	if x != nil {
		return x.Status
	}
	return 0
}

func (x *ValidateCredResp) GetToken() *Token {
	if x != nil {
		return x.Token
	}
	return nil
}

var File_auth_proto protoreflect.FileDescriptor

var file_auth_proto_rawDesc = []byte{
	0x0a, 0x0a, 0x61, 0x75, 0x74, 0x68, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x04, 0x61, 0x75,
	0x74, 0x68, 0x22, 0x41, 0x0a, 0x05, 0x54, 0x6f, 0x6b, 0x65, 0x6e, 0x12, 0x24, 0x0a, 0x06, 0x66,
	0x6c, 0x61, 0x76, 0x6f, 0x72, 0x18, 0x01, 0x20, 0x01, 0x28, 0x0e, 0x32, 0x0c, 0x2e, 0x61, 0x75,
	0x74, 0x68, 0x2e, 0x46, 0x6c, 0x61, 0x76, 0x6f, 0x72, 0x52, 0x06, 0x66, 0x6c, 0x61, 0x76, 0x6f,
	0x72, 0x12, 0x12, 0x0a, 0x04, 0x64, 0x61, 0x74, 0x61, 0x18, 0x02, 0x20, 0x01, 0x28, 0x0c, 0x52,
	0x04, 0x64, 0x61, 0x74, 0x61, 0x22, 0x97, 0x01, 0x0a, 0x03, 0x53, 0x79, 0x73, 0x12, 0x14, 0x0a,
	0x05, 0x73, 0x74, 0x61, 0x6d, 0x70, 0x18, 0x01, 0x20, 0x01, 0x28, 0x04, 0x52, 0x05, 0x73, 0x74,
	0x61, 0x6d, 0x70, 0x12, 0x20, 0x0a, 0x0b, 0x6d, 0x61, 0x63, 0x68, 0x69, 0x6e, 0x65, 0x6e, 0x61,
	0x6d, 0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0b, 0x6d, 0x61, 0x63, 0x68, 0x69, 0x6e,
	0x65, 0x6e, 0x61, 0x6d, 0x65, 0x12, 0x12, 0x0a, 0x04, 0x75, 0x73, 0x65, 0x72, 0x18, 0x03, 0x20,
	0x01, 0x28, 0x09, 0x52, 0x04, 0x75, 0x73, 0x65, 0x72, 0x12, 0x14, 0x0a, 0x05, 0x67, 0x72, 0x6f,
	0x75, 0x70, 0x18, 0x04, 0x20, 0x01, 0x28, 0x09, 0x52, 0x05, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x12,
	0x16, 0x0a, 0x06, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x73, 0x18, 0x05, 0x20, 0x03, 0x28, 0x09, 0x52,
	0x06, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x73, 0x12, 0x16, 0x0a, 0x06, 0x73, 0x65, 0x63, 0x63, 0x74,
	0x78, 0x18, 0x06, 0x20, 0x01, 0x28, 0x09, 0x52, 0x06, 0x73, 0x65, 0x63, 0x63, 0x74, 0x78, 0x22,
	0x70, 0x0a, 0x0a, 0x43, 0x72, 0x65, 0x64, 0x65, 0x6e, 0x74, 0x69, 0x61, 0x6c, 0x12, 0x21, 0x0a,
	0x05, 0x74, 0x6f, 0x6b, 0x65, 0x6e, 0x18, 0x01, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x0b, 0x2e, 0x61,
	0x75, 0x74, 0x68, 0x2e, 0x54, 0x6f, 0x6b, 0x65, 0x6e, 0x52, 0x05, 0x74, 0x6f, 0x6b, 0x65, 0x6e,
	0x12, 0x27, 0x0a, 0x08, 0x76, 0x65, 0x72, 0x69, 0x66, 0x69, 0x65, 0x72, 0x18, 0x02, 0x20, 0x01,
	0x28, 0x0b, 0x32, 0x0b, 0x2e, 0x61, 0x75, 0x74, 0x68, 0x2e, 0x54, 0x6f, 0x6b, 0x65, 0x6e, 0x52,
	0x08, 0x76, 0x65, 0x72, 0x69, 0x66, 0x69, 0x65, 0x72, 0x12, 0x16, 0x0a, 0x06, 0x6f, 0x72, 0x69,
	0x67, 0x69, 0x6e, 0x18, 0x03, 0x20, 0x01, 0x28, 0x09, 0x52, 0x06, 0x6f, 0x72, 0x69, 0x67, 0x69,
	0x6e, 0x22, 0x4b, 0x0a, 0x0b, 0x47, 0x65, 0x74, 0x43, 0x72, 0x65, 0x64, 0x52, 0x65, 0x73, 0x70,
	0x12, 0x16, 0x0a, 0x06, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73, 0x18, 0x01, 0x20, 0x01, 0x28, 0x05,
	0x52, 0x06, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73, 0x12, 0x24, 0x0a, 0x04, 0x63, 0x72, 0x65, 0x64,
	0x18, 0x02, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x10, 0x2e, 0x61, 0x75, 0x74, 0x68, 0x2e, 0x43, 0x72,
	0x65, 0x64, 0x65, 0x6e, 0x74, 0x69, 0x61, 0x6c, 0x52, 0x04, 0x63, 0x72, 0x65, 0x64, 0x22, 0x37,
	0x0a, 0x0f, 0x56, 0x61, 0x6c, 0x69, 0x64, 0x61, 0x74, 0x65, 0x43, 0x72, 0x65, 0x64, 0x52, 0x65,
	0x71, 0x12, 0x24, 0x0a, 0x04, 0x63, 0x72, 0x65, 0x64, 0x18, 0x01, 0x20, 0x01, 0x28, 0x0b, 0x32,
	0x10, 0x2e, 0x61, 0x75, 0x74, 0x68, 0x2e, 0x43, 0x72, 0x65, 0x64, 0x65, 0x6e, 0x74, 0x69, 0x61,
	0x6c, 0x52, 0x04, 0x63, 0x72, 0x65, 0x64, 0x22, 0x4d, 0x0a, 0x10, 0x56, 0x61, 0x6c, 0x69, 0x64,
	0x61, 0x74, 0x65, 0x43, 0x72, 0x65, 0x64, 0x52, 0x65, 0x73, 0x70, 0x12, 0x16, 0x0a, 0x06, 0x73,
	0x74, 0x61, 0x74, 0x75, 0x73, 0x18, 0x01, 0x20, 0x01, 0x28, 0x05, 0x52, 0x06, 0x73, 0x74, 0x61,
	0x74, 0x75, 0x73, 0x12, 0x21, 0x0a, 0x05, 0x74, 0x6f, 0x6b, 0x65, 0x6e, 0x18, 0x02, 0x20, 0x01,
	0x28, 0x0b, 0x32, 0x0b, 0x2e, 0x61, 0x75, 0x74, 0x68, 0x2e, 0x54, 0x6f, 0x6b, 0x65, 0x6e, 0x52,
	0x05, 0x74, 0x6f, 0x6b, 0x65, 0x6e, 0x2a, 0x25, 0x0a, 0x06, 0x46, 0x6c, 0x61, 0x76, 0x6f, 0x72,
	0x12, 0x0d, 0x0a, 0x09, 0x41, 0x55, 0x54, 0x48, 0x5f, 0x4e, 0x4f, 0x4e, 0x45, 0x10, 0x00, 0x12,
	0x0c, 0x0a, 0x08, 0x41, 0x55, 0x54, 0x48, 0x5f, 0x53, 0x59, 0x53, 0x10, 0x01, 0x42, 0x3b, 0x5a,
	0x39, 0x67, 0x69, 0x74, 0x68, 0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x64, 0x61, 0x6f, 0x73,
	0x2d, 0x73, 0x74, 0x61, 0x63, 0x6b, 0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2f, 0x73, 0x72, 0x63, 0x2f,
	0x63, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x2f, 0x73, 0x65, 0x63, 0x75, 0x72, 0x69, 0x74, 0x79,
	0x2f, 0x61, 0x75, 0x74, 0x68, 0x3b, 0x61, 0x75, 0x74, 0x68, 0x62, 0x06, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x33,
}

var (
	file_auth_proto_rawDescOnce sync.Once
	file_auth_proto_rawDescData = file_auth_proto_rawDesc
)

func file_auth_proto_rawDescGZIP() []byte {
	file_auth_proto_rawDescOnce.Do(func() {
		file_auth_proto_rawDescData = protoimpl.X.CompressGZIP(file_auth_proto_rawDescData)
	})
	return file_auth_proto_rawDescData
}

var file_auth_proto_enumTypes = make([]protoimpl.EnumInfo, 1)
var file_auth_proto_msgTypes = make([]protoimpl.MessageInfo, 6)
var file_auth_proto_goTypes = []interface{}{
	(Flavor)(0),              // 0: auth.Flavor
	(*Token)(nil),            // 1: auth.Token
	(*Sys)(nil),              // 2: auth.Sys
	(*Credential)(nil),       // 3: auth.Credential
	(*GetCredResp)(nil),      // 4: auth.GetCredResp
	(*ValidateCredReq)(nil),  // 5: auth.ValidateCredReq
	(*ValidateCredResp)(nil), // 6: auth.ValidateCredResp
}
var file_auth_proto_depIdxs = []int32{
	0, // 0: auth.Token.flavor:type_name -> auth.Flavor
	1, // 1: auth.Credential.token:type_name -> auth.Token
	1, // 2: auth.Credential.verifier:type_name -> auth.Token
	3, // 3: auth.GetCredResp.cred:type_name -> auth.Credential
	3, // 4: auth.ValidateCredReq.cred:type_name -> auth.Credential
	1, // 5: auth.ValidateCredResp.token:type_name -> auth.Token
	6, // [6:6] is the sub-list for method output_type
	6, // [6:6] is the sub-list for method input_type
	6, // [6:6] is the sub-list for extension type_name
	6, // [6:6] is the sub-list for extension extendee
	0, // [0:6] is the sub-list for field type_name
}

func init() { file_auth_proto_init() }
func file_auth_proto_init() {
	if File_auth_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_auth_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*Token); i {
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
		file_auth_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*Sys); i {
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
		file_auth_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*Credential); i {
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
		file_auth_proto_msgTypes[3].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*GetCredResp); i {
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
		file_auth_proto_msgTypes[4].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*ValidateCredReq); i {
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
		file_auth_proto_msgTypes[5].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*ValidateCredResp); i {
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
			RawDescriptor: file_auth_proto_rawDesc,
			NumEnums:      1,
			NumMessages:   6,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_auth_proto_goTypes,
		DependencyIndexes: file_auth_proto_depIdxs,
		EnumInfos:         file_auth_proto_enumTypes,
		MessageInfos:      file_auth_proto_msgTypes,
	}.Build()
	File_auth_proto = out.File
	file_auth_proto_rawDesc = nil
	file_auth_proto_goTypes = nil
	file_auth_proto_depIdxs = nil
}
