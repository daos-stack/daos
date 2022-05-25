//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.5.0
// source: mgmt/mgmt.proto

package mgmt

import (
	chk "github.com/daos-stack/daos/src/control/common/proto/chk"
	shared "github.com/daos-stack/daos/src/control/common/proto/shared"
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

var File_mgmt_mgmt_proto protoreflect.FileDescriptor

var file_mgmt_mgmt_proto_rawDesc = []byte{
	0x0a, 0x0f, 0x6d, 0x67, 0x6d, 0x74, 0x2f, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x12, 0x04, 0x6d, 0x67, 0x6d, 0x74, 0x1a, 0x12, 0x73, 0x68, 0x61, 0x72, 0x65, 0x64, 0x2f,
	0x65, 0x76, 0x65, 0x6e, 0x74, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x0f, 0x6d, 0x67, 0x6d,
	0x74, 0x2f, 0x70, 0x6f, 0x6f, 0x6c, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x10, 0x6d, 0x67,
	0x6d, 0x74, 0x2f, 0x63, 0x68, 0x65, 0x63, 0x6b, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x0f,
	0x6d, 0x67, 0x6d, 0x74, 0x2f, 0x63, 0x6f, 0x6e, 0x74, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a,
	0x0e, 0x6d, 0x67, 0x6d, 0x74, 0x2f, 0x73, 0x76, 0x63, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a,
	0x0e, 0x6d, 0x67, 0x6d, 0x74, 0x2f, 0x61, 0x63, 0x6c, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a,
	0x11, 0x6d, 0x67, 0x6d, 0x74, 0x2f, 0x73, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x2e, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x1a, 0x0d, 0x63, 0x68, 0x6b, 0x2f, 0x63, 0x68, 0x6b, 0x2e, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x32, 0x96, 0x12, 0x0a, 0x07, 0x4d, 0x67, 0x6d, 0x74, 0x53, 0x76, 0x63, 0x12, 0x27, 0x0a,
	0x04, 0x4a, 0x6f, 0x69, 0x6e, 0x12, 0x0d, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x4a, 0x6f, 0x69,
	0x6e, 0x52, 0x65, 0x71, 0x1a, 0x0e, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x4a, 0x6f, 0x69, 0x6e,
	0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x43, 0x0a, 0x0c, 0x43, 0x6c, 0x75, 0x73, 0x74, 0x65,
	0x72, 0x45, 0x76, 0x65, 0x6e, 0x74, 0x12, 0x17, 0x2e, 0x73, 0x68, 0x61, 0x72, 0x65, 0x64, 0x2e,
	0x43, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72, 0x45, 0x76, 0x65, 0x6e, 0x74, 0x52, 0x65, 0x71, 0x1a,
	0x18, 0x2e, 0x73, 0x68, 0x61, 0x72, 0x65, 0x64, 0x2e, 0x43, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72,
	0x45, 0x76, 0x65, 0x6e, 0x74, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0b, 0x4c,
	0x65, 0x61, 0x64, 0x65, 0x72, 0x51, 0x75, 0x65, 0x72, 0x79, 0x12, 0x14, 0x2e, 0x6d, 0x67, 0x6d,
	0x74, 0x2e, 0x4c, 0x65, 0x61, 0x64, 0x65, 0x72, 0x51, 0x75, 0x65, 0x72, 0x79, 0x52, 0x65, 0x71,
	0x1a, 0x15, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x4c, 0x65, 0x61, 0x64, 0x65, 0x72, 0x51, 0x75,
	0x65, 0x72, 0x79, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x39, 0x0a, 0x0a, 0x50, 0x6f, 0x6f,
	0x6c, 0x43, 0x72, 0x65, 0x61, 0x74, 0x65, 0x12, 0x13, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50,
	0x6f, 0x6f, 0x6c, 0x43, 0x72, 0x65, 0x61, 0x74, 0x65, 0x52, 0x65, 0x71, 0x1a, 0x14, 0x2e, 0x6d,
	0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x43, 0x72, 0x65, 0x61, 0x74, 0x65, 0x52, 0x65,
	0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0b, 0x50, 0x6f, 0x6f, 0x6c, 0x44, 0x65, 0x73, 0x74,
	0x72, 0x6f, 0x79, 0x12, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x44,
	0x65, 0x73, 0x74, 0x72, 0x6f, 0x79, 0x52, 0x65, 0x71, 0x1a, 0x15, 0x2e, 0x6d, 0x67, 0x6d, 0x74,
	0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x44, 0x65, 0x73, 0x74, 0x72, 0x6f, 0x79, 0x52, 0x65, 0x73, 0x70,
	0x22, 0x00, 0x12, 0x36, 0x0a, 0x09, 0x50, 0x6f, 0x6f, 0x6c, 0x45, 0x76, 0x69, 0x63, 0x74, 0x12,
	0x12, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x45, 0x76, 0x69, 0x63, 0x74,
	0x52, 0x65, 0x71, 0x1a, 0x13, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x45,
	0x76, 0x69, 0x63, 0x74, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0b, 0x50, 0x6f,
	0x6f, 0x6c, 0x45, 0x78, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x12, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74,
	0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x45, 0x78, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x52, 0x65, 0x71, 0x1a,
	0x15, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x45, 0x78, 0x63, 0x6c, 0x75,
	0x64, 0x65, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x36, 0x0a, 0x09, 0x50, 0x6f, 0x6f, 0x6c,
	0x44, 0x72, 0x61, 0x69, 0x6e, 0x12, 0x12, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f,
	0x6c, 0x44, 0x72, 0x61, 0x69, 0x6e, 0x52, 0x65, 0x71, 0x1a, 0x13, 0x2e, 0x6d, 0x67, 0x6d, 0x74,
	0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x44, 0x72, 0x61, 0x69, 0x6e, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00,
	0x12, 0x39, 0x0a, 0x0a, 0x50, 0x6f, 0x6f, 0x6c, 0x45, 0x78, 0x74, 0x65, 0x6e, 0x64, 0x12, 0x13,
	0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x45, 0x78, 0x74, 0x65, 0x6e, 0x64,
	0x52, 0x65, 0x71, 0x1a, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x45,
	0x78, 0x74, 0x65, 0x6e, 0x64, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x48, 0x0a, 0x0f, 0x50,
	0x6f, 0x6f, 0x6c, 0x52, 0x65, 0x69, 0x6e, 0x74, 0x65, 0x67, 0x72, 0x61, 0x74, 0x65, 0x12, 0x18,
	0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x52, 0x65, 0x69, 0x6e, 0x74, 0x65,
	0x67, 0x72, 0x61, 0x74, 0x65, 0x52, 0x65, 0x71, 0x1a, 0x19, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e,
	0x50, 0x6f, 0x6f, 0x6c, 0x52, 0x65, 0x69, 0x6e, 0x74, 0x65, 0x67, 0x72, 0x61, 0x74, 0x65, 0x52,
	0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x36, 0x0a, 0x09, 0x50, 0x6f, 0x6f, 0x6c, 0x51, 0x75, 0x65,
	0x72, 0x79, 0x12, 0x12, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x51, 0x75,
	0x65, 0x72, 0x79, 0x52, 0x65, 0x71, 0x1a, 0x13, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f,
	0x6f, 0x6c, 0x51, 0x75, 0x65, 0x72, 0x79, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x48, 0x0a,
	0x0f, 0x50, 0x6f, 0x6f, 0x6c, 0x51, 0x75, 0x65, 0x72, 0x79, 0x54, 0x61, 0x72, 0x67, 0x65, 0x74,
	0x12, 0x18, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x51, 0x75, 0x65, 0x72,
	0x79, 0x54, 0x61, 0x72, 0x67, 0x65, 0x74, 0x52, 0x65, 0x71, 0x1a, 0x19, 0x2e, 0x6d, 0x67, 0x6d,
	0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x51, 0x75, 0x65, 0x72, 0x79, 0x54, 0x61, 0x72, 0x67, 0x65,
	0x74, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0b, 0x50, 0x6f, 0x6f, 0x6c, 0x53,
	0x65, 0x74, 0x50, 0x72, 0x6f, 0x70, 0x12, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f,
	0x6f, 0x6c, 0x53, 0x65, 0x74, 0x50, 0x72, 0x6f, 0x70, 0x52, 0x65, 0x71, 0x1a, 0x15, 0x2e, 0x6d,
	0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x53, 0x65, 0x74, 0x50, 0x72, 0x6f, 0x70, 0x52,
	0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0b, 0x50, 0x6f, 0x6f, 0x6c, 0x47, 0x65, 0x74,
	0x50, 0x72, 0x6f, 0x70, 0x12, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c,
	0x47, 0x65, 0x74, 0x50, 0x72, 0x6f, 0x70, 0x52, 0x65, 0x71, 0x1a, 0x15, 0x2e, 0x6d, 0x67, 0x6d,
	0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x47, 0x65, 0x74, 0x50, 0x72, 0x6f, 0x70, 0x52, 0x65, 0x73,
	0x70, 0x22, 0x00, 0x12, 0x2e, 0x0a, 0x0a, 0x50, 0x6f, 0x6f, 0x6c, 0x47, 0x65, 0x74, 0x41, 0x43,
	0x4c, 0x12, 0x0f, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x47, 0x65, 0x74, 0x41, 0x43, 0x4c, 0x52,
	0x65, 0x71, 0x1a, 0x0d, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x41, 0x43, 0x4c, 0x52, 0x65, 0x73,
	0x70, 0x22, 0x00, 0x12, 0x37, 0x0a, 0x10, 0x50, 0x6f, 0x6f, 0x6c, 0x4f, 0x76, 0x65, 0x72, 0x77,
	0x72, 0x69, 0x74, 0x65, 0x41, 0x43, 0x4c, 0x12, 0x12, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x4d,
	0x6f, 0x64, 0x69, 0x66, 0x79, 0x41, 0x43, 0x4c, 0x52, 0x65, 0x71, 0x1a, 0x0d, 0x2e, 0x6d, 0x67,
	0x6d, 0x74, 0x2e, 0x41, 0x43, 0x4c, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x34, 0x0a, 0x0d,
	0x50, 0x6f, 0x6f, 0x6c, 0x55, 0x70, 0x64, 0x61, 0x74, 0x65, 0x41, 0x43, 0x4c, 0x12, 0x12, 0x2e,
	0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x4d, 0x6f, 0x64, 0x69, 0x66, 0x79, 0x41, 0x43, 0x4c, 0x52, 0x65,
	0x71, 0x1a, 0x0d, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x41, 0x43, 0x4c, 0x52, 0x65, 0x73, 0x70,
	0x22, 0x00, 0x12, 0x34, 0x0a, 0x0d, 0x50, 0x6f, 0x6f, 0x6c, 0x44, 0x65, 0x6c, 0x65, 0x74, 0x65,
	0x41, 0x43, 0x4c, 0x12, 0x12, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x44, 0x65, 0x6c, 0x65, 0x74,
	0x65, 0x41, 0x43, 0x4c, 0x52, 0x65, 0x71, 0x1a, 0x0d, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x41,
	0x43, 0x4c, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x42, 0x0a, 0x0d, 0x47, 0x65, 0x74, 0x41,
	0x74, 0x74, 0x61, 0x63, 0x68, 0x49, 0x6e, 0x66, 0x6f, 0x12, 0x16, 0x2e, 0x6d, 0x67, 0x6d, 0x74,
	0x2e, 0x47, 0x65, 0x74, 0x41, 0x74, 0x74, 0x61, 0x63, 0x68, 0x49, 0x6e, 0x66, 0x6f, 0x52, 0x65,
	0x71, 0x1a, 0x17, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x47, 0x65, 0x74, 0x41, 0x74, 0x74, 0x61,
	0x63, 0x68, 0x49, 0x6e, 0x66, 0x6f, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x36, 0x0a, 0x09,
	0x4c, 0x69, 0x73, 0x74, 0x50, 0x6f, 0x6f, 0x6c, 0x73, 0x12, 0x12, 0x2e, 0x6d, 0x67, 0x6d, 0x74,
	0x2e, 0x4c, 0x69, 0x73, 0x74, 0x50, 0x6f, 0x6f, 0x6c, 0x73, 0x52, 0x65, 0x71, 0x1a, 0x13, 0x2e,
	0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x4c, 0x69, 0x73, 0x74, 0x50, 0x6f, 0x6f, 0x6c, 0x73, 0x52, 0x65,
	0x73, 0x70, 0x22, 0x00, 0x12, 0x39, 0x0a, 0x0e, 0x4c, 0x69, 0x73, 0x74, 0x43, 0x6f, 0x6e, 0x74,
	0x61, 0x69, 0x6e, 0x65, 0x72, 0x73, 0x12, 0x11, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x4c, 0x69,
	0x73, 0x74, 0x43, 0x6f, 0x6e, 0x74, 0x52, 0x65, 0x71, 0x1a, 0x12, 0x2e, 0x6d, 0x67, 0x6d, 0x74,
	0x2e, 0x4c, 0x69, 0x73, 0x74, 0x43, 0x6f, 0x6e, 0x74, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12,
	0x3f, 0x0a, 0x0c, 0x43, 0x6f, 0x6e, 0x74, 0x53, 0x65, 0x74, 0x4f, 0x77, 0x6e, 0x65, 0x72, 0x12,
	0x15, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x6f, 0x6e, 0x74, 0x53, 0x65, 0x74, 0x4f, 0x77,
	0x6e, 0x65, 0x72, 0x52, 0x65, 0x71, 0x1a, 0x16, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x6f,
	0x6e, 0x74, 0x53, 0x65, 0x74, 0x4f, 0x77, 0x6e, 0x65, 0x72, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00,
	0x12, 0x3c, 0x0a, 0x0b, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x51, 0x75, 0x65, 0x72, 0x79, 0x12,
	0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x51, 0x75, 0x65,
	0x72, 0x79, 0x52, 0x65, 0x71, 0x1a, 0x15, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73,
	0x74, 0x65, 0x6d, 0x51, 0x75, 0x65, 0x72, 0x79, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x39,
	0x0a, 0x0a, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x53, 0x74, 0x6f, 0x70, 0x12, 0x13, 0x2e, 0x6d,
	0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x53, 0x74, 0x6f, 0x70, 0x52, 0x65,
	0x71, 0x1a, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x53,
	0x74, 0x6f, 0x70, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0b, 0x53, 0x79, 0x73,
	0x74, 0x65, 0x6d, 0x53, 0x74, 0x61, 0x72, 0x74, 0x12, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e,
	0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x53, 0x74, 0x61, 0x72, 0x74, 0x52, 0x65, 0x71, 0x1a, 0x15,
	0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x53, 0x74, 0x61, 0x72,
	0x74, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0b, 0x53, 0x79, 0x73, 0x74, 0x65,
	0x6d, 0x45, 0x72, 0x61, 0x73, 0x65, 0x12, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79,
	0x73, 0x74, 0x65, 0x6d, 0x45, 0x72, 0x61, 0x73, 0x65, 0x52, 0x65, 0x71, 0x1a, 0x15, 0x2e, 0x6d,
	0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x45, 0x72, 0x61, 0x73, 0x65, 0x52,
	0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x42, 0x0a, 0x0d, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x43,
	0x6c, 0x65, 0x61, 0x6e, 0x75, 0x70, 0x12, 0x16, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79,
	0x73, 0x74, 0x65, 0x6d, 0x43, 0x6c, 0x65, 0x61, 0x6e, 0x75, 0x70, 0x52, 0x65, 0x71, 0x1a, 0x17,
	0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x43, 0x6c, 0x65, 0x61,
	0x6e, 0x75, 0x70, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3f, 0x0a, 0x10, 0x53, 0x79, 0x73,
	0x74, 0x65, 0x6d, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x53, 0x74, 0x61, 0x72, 0x74, 0x12, 0x13, 0x2e,
	0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x53, 0x74, 0x61, 0x72, 0x74, 0x52,
	0x65, 0x71, 0x1a, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x53,
	0x74, 0x61, 0x72, 0x74, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0f, 0x53, 0x79,
	0x73, 0x74, 0x65, 0x6d, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x53, 0x74, 0x6f, 0x70, 0x12, 0x12, 0x2e,
	0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x53, 0x74, 0x6f, 0x70, 0x52, 0x65,
	0x71, 0x1a, 0x13, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x53, 0x74,
	0x6f, 0x70, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3f, 0x0a, 0x10, 0x53, 0x79, 0x73, 0x74,
	0x65, 0x6d, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x51, 0x75, 0x65, 0x72, 0x79, 0x12, 0x13, 0x2e, 0x6d,
	0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x51, 0x75, 0x65, 0x72, 0x79, 0x52, 0x65,
	0x71, 0x1a, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x51, 0x75,
	0x65, 0x72, 0x79, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0f, 0x53, 0x79, 0x73,
	0x74, 0x65, 0x6d, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x50, 0x72, 0x6f, 0x70, 0x12, 0x12, 0x2e, 0x6d,
	0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x50, 0x72, 0x6f, 0x70, 0x52, 0x65, 0x71,
	0x1a, 0x13, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x50, 0x72, 0x6f,
	0x70, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x11, 0x53, 0x79, 0x73, 0x74, 0x65,
	0x6d, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x52, 0x65, 0x70, 0x61, 0x69, 0x72, 0x12, 0x11, 0x2e, 0x6d,
	0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x41, 0x63, 0x74, 0x52, 0x65, 0x71, 0x1a,
	0x12, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x43, 0x68, 0x65, 0x63, 0x6b, 0x41, 0x63, 0x74, 0x52,
	0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x3c, 0x0a, 0x0b, 0x50, 0x6f, 0x6f, 0x6c, 0x55, 0x70, 0x67,
	0x72, 0x61, 0x64, 0x65, 0x12, 0x14, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c,
	0x55, 0x70, 0x67, 0x72, 0x61, 0x64, 0x65, 0x52, 0x65, 0x71, 0x1a, 0x15, 0x2e, 0x6d, 0x67, 0x6d,
	0x74, 0x2e, 0x50, 0x6f, 0x6f, 0x6c, 0x55, 0x70, 0x67, 0x72, 0x61, 0x64, 0x65, 0x52, 0x65, 0x73,
	0x70, 0x22, 0x00, 0x12, 0x37, 0x0a, 0x11, 0x46, 0x61, 0x75, 0x6c, 0x74, 0x49, 0x6e, 0x6a, 0x65,
	0x63, 0x74, 0x52, 0x65, 0x70, 0x6f, 0x72, 0x74, 0x12, 0x10, 0x2e, 0x63, 0x68, 0x6b, 0x2e, 0x43,
	0x68, 0x65, 0x63, 0x6b, 0x52, 0x65, 0x70, 0x6f, 0x72, 0x74, 0x1a, 0x0e, 0x2e, 0x6d, 0x67, 0x6d,
	0x74, 0x2e, 0x44, 0x61, 0x6f, 0x73, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x39, 0x0a, 0x0d,
	0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x53, 0x65, 0x74, 0x41, 0x74, 0x74, 0x72, 0x12, 0x16, 0x2e,
	0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x53, 0x65, 0x74, 0x41, 0x74,
	0x74, 0x72, 0x52, 0x65, 0x71, 0x1a, 0x0e, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x44, 0x61, 0x6f,
	0x73, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x42, 0x0a, 0x0d, 0x53, 0x79, 0x73, 0x74, 0x65,
	0x6d, 0x47, 0x65, 0x74, 0x41, 0x74, 0x74, 0x72, 0x12, 0x16, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e,
	0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x47, 0x65, 0x74, 0x41, 0x74, 0x74, 0x72, 0x52, 0x65, 0x71,
	0x1a, 0x17, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x47, 0x65,
	0x74, 0x41, 0x74, 0x74, 0x72, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x39, 0x0a, 0x0d, 0x53,
	0x79, 0x73, 0x74, 0x65, 0x6d, 0x53, 0x65, 0x74, 0x50, 0x72, 0x6f, 0x70, 0x12, 0x16, 0x2e, 0x6d,
	0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x53, 0x65, 0x74, 0x50, 0x72, 0x6f,
	0x70, 0x52, 0x65, 0x71, 0x1a, 0x0e, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x44, 0x61, 0x6f, 0x73,
	0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x12, 0x42, 0x0a, 0x0d, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d,
	0x47, 0x65, 0x74, 0x50, 0x72, 0x6f, 0x70, 0x12, 0x16, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53,
	0x79, 0x73, 0x74, 0x65, 0x6d, 0x47, 0x65, 0x74, 0x50, 0x72, 0x6f, 0x70, 0x52, 0x65, 0x71, 0x1a,
	0x17, 0x2e, 0x6d, 0x67, 0x6d, 0x74, 0x2e, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x47, 0x65, 0x74,
	0x50, 0x72, 0x6f, 0x70, 0x52, 0x65, 0x73, 0x70, 0x22, 0x00, 0x42, 0x3a, 0x5a, 0x38, 0x67, 0x69,
	0x74, 0x68, 0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2d, 0x73, 0x74,
	0x61, 0x63, 0x6b, 0x2f, 0x64, 0x61, 0x6f, 0x73, 0x2f, 0x73, 0x72, 0x63, 0x2f, 0x63, 0x6f, 0x6e,
	0x74, 0x72, 0x6f, 0x6c, 0x2f, 0x63, 0x6f, 0x6d, 0x6d, 0x6f, 0x6e, 0x2f, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x2f, 0x6d, 0x67, 0x6d, 0x74, 0x62, 0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var file_mgmt_mgmt_proto_goTypes = []interface{}{
	(*JoinReq)(nil),                 // 0: mgmt.JoinReq
	(*shared.ClusterEventReq)(nil),  // 1: shared.ClusterEventReq
	(*LeaderQueryReq)(nil),          // 2: mgmt.LeaderQueryReq
	(*PoolCreateReq)(nil),           // 3: mgmt.PoolCreateReq
	(*PoolDestroyReq)(nil),          // 4: mgmt.PoolDestroyReq
	(*PoolEvictReq)(nil),            // 5: mgmt.PoolEvictReq
	(*PoolExcludeReq)(nil),          // 6: mgmt.PoolExcludeReq
	(*PoolDrainReq)(nil),            // 7: mgmt.PoolDrainReq
	(*PoolExtendReq)(nil),           // 8: mgmt.PoolExtendReq
	(*PoolReintegrateReq)(nil),      // 9: mgmt.PoolReintegrateReq
	(*PoolQueryReq)(nil),            // 10: mgmt.PoolQueryReq
	(*PoolQueryTargetReq)(nil),      // 11: mgmt.PoolQueryTargetReq
	(*PoolSetPropReq)(nil),          // 12: mgmt.PoolSetPropReq
	(*PoolGetPropReq)(nil),          // 13: mgmt.PoolGetPropReq
	(*GetACLReq)(nil),               // 14: mgmt.GetACLReq
	(*ModifyACLReq)(nil),            // 15: mgmt.ModifyACLReq
	(*DeleteACLReq)(nil),            // 16: mgmt.DeleteACLReq
	(*GetAttachInfoReq)(nil),        // 17: mgmt.GetAttachInfoReq
	(*ListPoolsReq)(nil),            // 18: mgmt.ListPoolsReq
	(*ListContReq)(nil),             // 19: mgmt.ListContReq
	(*ContSetOwnerReq)(nil),         // 20: mgmt.ContSetOwnerReq
	(*SystemQueryReq)(nil),          // 21: mgmt.SystemQueryReq
	(*SystemStopReq)(nil),           // 22: mgmt.SystemStopReq
	(*SystemStartReq)(nil),          // 23: mgmt.SystemStartReq
	(*SystemEraseReq)(nil),          // 24: mgmt.SystemEraseReq
	(*SystemCleanupReq)(nil),        // 25: mgmt.SystemCleanupReq
	(*CheckStartReq)(nil),           // 26: mgmt.CheckStartReq
	(*CheckStopReq)(nil),            // 27: mgmt.CheckStopReq
	(*CheckQueryReq)(nil),           // 28: mgmt.CheckQueryReq
	(*CheckPropReq)(nil),            // 29: mgmt.CheckPropReq
	(*CheckActReq)(nil),             // 30: mgmt.CheckActReq
	(*PoolUpgradeReq)(nil),          // 31: mgmt.PoolUpgradeReq
	(*chk.CheckReport)(nil),         // 32: chk.CheckReport
	(*SystemSetAttrReq)(nil),        // 33: mgmt.SystemSetAttrReq
	(*SystemGetAttrReq)(nil),        // 34: mgmt.SystemGetAttrReq
	(*SystemSetPropReq)(nil),        // 35: mgmt.SystemSetPropReq
	(*SystemGetPropReq)(nil),        // 36: mgmt.SystemGetPropReq
	(*JoinResp)(nil),                // 37: mgmt.JoinResp
	(*shared.ClusterEventResp)(nil), // 38: shared.ClusterEventResp
	(*LeaderQueryResp)(nil),         // 39: mgmt.LeaderQueryResp
	(*PoolCreateResp)(nil),          // 40: mgmt.PoolCreateResp
	(*PoolDestroyResp)(nil),         // 41: mgmt.PoolDestroyResp
	(*PoolEvictResp)(nil),           // 42: mgmt.PoolEvictResp
	(*PoolExcludeResp)(nil),         // 43: mgmt.PoolExcludeResp
	(*PoolDrainResp)(nil),           // 44: mgmt.PoolDrainResp
	(*PoolExtendResp)(nil),          // 45: mgmt.PoolExtendResp
	(*PoolReintegrateResp)(nil),     // 46: mgmt.PoolReintegrateResp
	(*PoolQueryResp)(nil),           // 47: mgmt.PoolQueryResp
	(*PoolQueryTargetResp)(nil),     // 48: mgmt.PoolQueryTargetResp
	(*PoolSetPropResp)(nil),         // 49: mgmt.PoolSetPropResp
	(*PoolGetPropResp)(nil),         // 50: mgmt.PoolGetPropResp
	(*ACLResp)(nil),                 // 51: mgmt.ACLResp
	(*GetAttachInfoResp)(nil),       // 52: mgmt.GetAttachInfoResp
	(*ListPoolsResp)(nil),           // 53: mgmt.ListPoolsResp
	(*ListContResp)(nil),            // 54: mgmt.ListContResp
	(*ContSetOwnerResp)(nil),        // 55: mgmt.ContSetOwnerResp
	(*SystemQueryResp)(nil),         // 56: mgmt.SystemQueryResp
	(*SystemStopResp)(nil),          // 57: mgmt.SystemStopResp
	(*SystemStartResp)(nil),         // 58: mgmt.SystemStartResp
	(*SystemEraseResp)(nil),         // 59: mgmt.SystemEraseResp
	(*SystemCleanupResp)(nil),       // 60: mgmt.SystemCleanupResp
	(*CheckStartResp)(nil),          // 61: mgmt.CheckStartResp
	(*CheckStopResp)(nil),           // 62: mgmt.CheckStopResp
	(*CheckQueryResp)(nil),          // 63: mgmt.CheckQueryResp
	(*CheckPropResp)(nil),           // 64: mgmt.CheckPropResp
	(*CheckActResp)(nil),            // 65: mgmt.CheckActResp
	(*PoolUpgradeResp)(nil),         // 66: mgmt.PoolUpgradeResp
	(*DaosResp)(nil),                // 67: mgmt.DaosResp
	(*SystemGetAttrResp)(nil),       // 68: mgmt.SystemGetAttrResp
	(*SystemGetPropResp)(nil),       // 69: mgmt.SystemGetPropResp
}
var file_mgmt_mgmt_proto_depIdxs = []int32{
	0,  // 0: mgmt.MgmtSvc.Join:input_type -> mgmt.JoinReq
	1,  // 1: mgmt.MgmtSvc.ClusterEvent:input_type -> shared.ClusterEventReq
	2,  // 2: mgmt.MgmtSvc.LeaderQuery:input_type -> mgmt.LeaderQueryReq
	3,  // 3: mgmt.MgmtSvc.PoolCreate:input_type -> mgmt.PoolCreateReq
	4,  // 4: mgmt.MgmtSvc.PoolDestroy:input_type -> mgmt.PoolDestroyReq
	5,  // 5: mgmt.MgmtSvc.PoolEvict:input_type -> mgmt.PoolEvictReq
	6,  // 6: mgmt.MgmtSvc.PoolExclude:input_type -> mgmt.PoolExcludeReq
	7,  // 7: mgmt.MgmtSvc.PoolDrain:input_type -> mgmt.PoolDrainReq
	8,  // 8: mgmt.MgmtSvc.PoolExtend:input_type -> mgmt.PoolExtendReq
	9,  // 9: mgmt.MgmtSvc.PoolReintegrate:input_type -> mgmt.PoolReintegrateReq
	10, // 10: mgmt.MgmtSvc.PoolQuery:input_type -> mgmt.PoolQueryReq
	11, // 11: mgmt.MgmtSvc.PoolQueryTarget:input_type -> mgmt.PoolQueryTargetReq
	12, // 12: mgmt.MgmtSvc.PoolSetProp:input_type -> mgmt.PoolSetPropReq
	13, // 13: mgmt.MgmtSvc.PoolGetProp:input_type -> mgmt.PoolGetPropReq
	14, // 14: mgmt.MgmtSvc.PoolGetACL:input_type -> mgmt.GetACLReq
	15, // 15: mgmt.MgmtSvc.PoolOverwriteACL:input_type -> mgmt.ModifyACLReq
	15, // 16: mgmt.MgmtSvc.PoolUpdateACL:input_type -> mgmt.ModifyACLReq
	16, // 17: mgmt.MgmtSvc.PoolDeleteACL:input_type -> mgmt.DeleteACLReq
	17, // 18: mgmt.MgmtSvc.GetAttachInfo:input_type -> mgmt.GetAttachInfoReq
	18, // 19: mgmt.MgmtSvc.ListPools:input_type -> mgmt.ListPoolsReq
	19, // 20: mgmt.MgmtSvc.ListContainers:input_type -> mgmt.ListContReq
	20, // 21: mgmt.MgmtSvc.ContSetOwner:input_type -> mgmt.ContSetOwnerReq
	21, // 22: mgmt.MgmtSvc.SystemQuery:input_type -> mgmt.SystemQueryReq
	22, // 23: mgmt.MgmtSvc.SystemStop:input_type -> mgmt.SystemStopReq
	23, // 24: mgmt.MgmtSvc.SystemStart:input_type -> mgmt.SystemStartReq
	24, // 25: mgmt.MgmtSvc.SystemErase:input_type -> mgmt.SystemEraseReq
	25, // 26: mgmt.MgmtSvc.SystemCleanup:input_type -> mgmt.SystemCleanupReq
	26, // 27: mgmt.MgmtSvc.SystemCheckStart:input_type -> mgmt.CheckStartReq
	27, // 28: mgmt.MgmtSvc.SystemCheckStop:input_type -> mgmt.CheckStopReq
	28, // 29: mgmt.MgmtSvc.SystemCheckQuery:input_type -> mgmt.CheckQueryReq
	29, // 30: mgmt.MgmtSvc.SystemCheckProp:input_type -> mgmt.CheckPropReq
	30, // 31: mgmt.MgmtSvc.SystemCheckRepair:input_type -> mgmt.CheckActReq
	31, // 32: mgmt.MgmtSvc.PoolUpgrade:input_type -> mgmt.PoolUpgradeReq
	32, // 33: mgmt.MgmtSvc.FaultInjectReport:input_type -> chk.CheckReport
	33, // 34: mgmt.MgmtSvc.SystemSetAttr:input_type -> mgmt.SystemSetAttrReq
	34, // 35: mgmt.MgmtSvc.SystemGetAttr:input_type -> mgmt.SystemGetAttrReq
	35, // 36: mgmt.MgmtSvc.SystemSetProp:input_type -> mgmt.SystemSetPropReq
	36, // 37: mgmt.MgmtSvc.SystemGetProp:input_type -> mgmt.SystemGetPropReq
	37, // 38: mgmt.MgmtSvc.Join:output_type -> mgmt.JoinResp
	38, // 39: mgmt.MgmtSvc.ClusterEvent:output_type -> shared.ClusterEventResp
	39, // 40: mgmt.MgmtSvc.LeaderQuery:output_type -> mgmt.LeaderQueryResp
	40, // 41: mgmt.MgmtSvc.PoolCreate:output_type -> mgmt.PoolCreateResp
	41, // 42: mgmt.MgmtSvc.PoolDestroy:output_type -> mgmt.PoolDestroyResp
	42, // 43: mgmt.MgmtSvc.PoolEvict:output_type -> mgmt.PoolEvictResp
	43, // 44: mgmt.MgmtSvc.PoolExclude:output_type -> mgmt.PoolExcludeResp
	44, // 45: mgmt.MgmtSvc.PoolDrain:output_type -> mgmt.PoolDrainResp
	45, // 46: mgmt.MgmtSvc.PoolExtend:output_type -> mgmt.PoolExtendResp
	46, // 47: mgmt.MgmtSvc.PoolReintegrate:output_type -> mgmt.PoolReintegrateResp
	47, // 48: mgmt.MgmtSvc.PoolQuery:output_type -> mgmt.PoolQueryResp
	48, // 49: mgmt.MgmtSvc.PoolQueryTarget:output_type -> mgmt.PoolQueryTargetResp
	49, // 50: mgmt.MgmtSvc.PoolSetProp:output_type -> mgmt.PoolSetPropResp
	50, // 51: mgmt.MgmtSvc.PoolGetProp:output_type -> mgmt.PoolGetPropResp
	51, // 52: mgmt.MgmtSvc.PoolGetACL:output_type -> mgmt.ACLResp
	51, // 53: mgmt.MgmtSvc.PoolOverwriteACL:output_type -> mgmt.ACLResp
	51, // 54: mgmt.MgmtSvc.PoolUpdateACL:output_type -> mgmt.ACLResp
	51, // 55: mgmt.MgmtSvc.PoolDeleteACL:output_type -> mgmt.ACLResp
	52, // 56: mgmt.MgmtSvc.GetAttachInfo:output_type -> mgmt.GetAttachInfoResp
	53, // 57: mgmt.MgmtSvc.ListPools:output_type -> mgmt.ListPoolsResp
	54, // 58: mgmt.MgmtSvc.ListContainers:output_type -> mgmt.ListContResp
	55, // 59: mgmt.MgmtSvc.ContSetOwner:output_type -> mgmt.ContSetOwnerResp
	56, // 60: mgmt.MgmtSvc.SystemQuery:output_type -> mgmt.SystemQueryResp
	57, // 61: mgmt.MgmtSvc.SystemStop:output_type -> mgmt.SystemStopResp
	58, // 62: mgmt.MgmtSvc.SystemStart:output_type -> mgmt.SystemStartResp
	59, // 63: mgmt.MgmtSvc.SystemErase:output_type -> mgmt.SystemEraseResp
	60, // 64: mgmt.MgmtSvc.SystemCleanup:output_type -> mgmt.SystemCleanupResp
	61, // 65: mgmt.MgmtSvc.SystemCheckStart:output_type -> mgmt.CheckStartResp
	62, // 66: mgmt.MgmtSvc.SystemCheckStop:output_type -> mgmt.CheckStopResp
	63, // 67: mgmt.MgmtSvc.SystemCheckQuery:output_type -> mgmt.CheckQueryResp
	64, // 68: mgmt.MgmtSvc.SystemCheckProp:output_type -> mgmt.CheckPropResp
	65, // 69: mgmt.MgmtSvc.SystemCheckRepair:output_type -> mgmt.CheckActResp
	66, // 70: mgmt.MgmtSvc.PoolUpgrade:output_type -> mgmt.PoolUpgradeResp
	67, // 71: mgmt.MgmtSvc.FaultInjectReport:output_type -> mgmt.DaosResp
	67, // 72: mgmt.MgmtSvc.SystemSetAttr:output_type -> mgmt.DaosResp
	68, // 73: mgmt.MgmtSvc.SystemGetAttr:output_type -> mgmt.SystemGetAttrResp
	67, // 74: mgmt.MgmtSvc.SystemSetProp:output_type -> mgmt.DaosResp
	69, // 75: mgmt.MgmtSvc.SystemGetProp:output_type -> mgmt.SystemGetPropResp
	38, // [38:76] is the sub-list for method output_type
	0,  // [0:38] is the sub-list for method input_type
	0,  // [0:0] is the sub-list for extension type_name
	0,  // [0:0] is the sub-list for extension extendee
	0,  // [0:0] is the sub-list for field type_name
}

func init() { file_mgmt_mgmt_proto_init() }
func file_mgmt_mgmt_proto_init() {
	if File_mgmt_mgmt_proto != nil {
		return
	}
	file_mgmt_pool_proto_init()
	file_mgmt_check_proto_init()
	file_mgmt_cont_proto_init()
	file_mgmt_svc_proto_init()
	file_mgmt_acl_proto_init()
	file_mgmt_system_proto_init()
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_mgmt_mgmt_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   0,
			NumExtensions: 0,
			NumServices:   1,
		},
		GoTypes:           file_mgmt_mgmt_proto_goTypes,
		DependencyIndexes: file_mgmt_mgmt_proto_depIdxs,
	}.Build()
	File_mgmt_mgmt_proto = out.File
	file_mgmt_mgmt_proto_rawDesc = nil
	file_mgmt_mgmt_proto_goTypes = nil
	file_mgmt_mgmt_proto_depIdxs = nil
}
