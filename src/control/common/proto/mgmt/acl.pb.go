// Code generated by protoc-gen-go. DO NOT EDIT.
// source: acl.proto

package mgmt

import proto "github.com/golang/protobuf/proto"
import fmt "fmt"
import math "math"

// Reference imports to suppress errors if they are not otherwise used.
var _ = proto.Marshal
var _ = fmt.Errorf
var _ = math.Inf

// This is a compile-time assertion to ensure that this generated file
// is compatible with the proto package it is being compiled against.
// A compilation error at this line likely means your copy of the
// proto package needs to be updated.
const _ = proto.ProtoPackageIsVersion2 // please upgrade the proto package

// Response to ACL-related requests includes the command status and current ACL
type ACLResp struct {
	Status               int32    `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	ACL                  []string `protobuf:"bytes,2,rep,name=ACL,proto3" json:"ACL,omitempty"`
	OwnerUser            string   `protobuf:"bytes,3,opt,name=ownerUser,proto3" json:"ownerUser,omitempty"`
	OwnerGroup           string   `protobuf:"bytes,4,opt,name=ownerGroup,proto3" json:"ownerGroup,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ACLResp) Reset()         { *m = ACLResp{} }
func (m *ACLResp) String() string { return proto.CompactTextString(m) }
func (*ACLResp) ProtoMessage()    {}
func (*ACLResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_acl_d5c9fbb52c9214a2, []int{0}
}
func (m *ACLResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ACLResp.Unmarshal(m, b)
}
func (m *ACLResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ACLResp.Marshal(b, m, deterministic)
}
func (dst *ACLResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ACLResp.Merge(dst, src)
}
func (m *ACLResp) XXX_Size() int {
	return xxx_messageInfo_ACLResp.Size(m)
}
func (m *ACLResp) XXX_DiscardUnknown() {
	xxx_messageInfo_ACLResp.DiscardUnknown(m)
}

var xxx_messageInfo_ACLResp proto.InternalMessageInfo

func (m *ACLResp) GetStatus() int32 {
	if m != nil {
		return m.Status
	}
	return 0
}

func (m *ACLResp) GetACL() []string {
	if m != nil {
		return m.ACL
	}
	return nil
}

func (m *ACLResp) GetOwnerUser() string {
	if m != nil {
		return m.OwnerUser
	}
	return ""
}

func (m *ACLResp) GetOwnerGroup() string {
	if m != nil {
		return m.OwnerGroup
	}
	return ""
}

// Request to fetch an ACL
type GetACLReq struct {
	Uuid                 string   `protobuf:"bytes,1,opt,name=uuid,proto3" json:"uuid,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *GetACLReq) Reset()         { *m = GetACLReq{} }
func (m *GetACLReq) String() string { return proto.CompactTextString(m) }
func (*GetACLReq) ProtoMessage()    {}
func (*GetACLReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_acl_d5c9fbb52c9214a2, []int{1}
}
func (m *GetACLReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_GetACLReq.Unmarshal(m, b)
}
func (m *GetACLReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_GetACLReq.Marshal(b, m, deterministic)
}
func (dst *GetACLReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_GetACLReq.Merge(dst, src)
}
func (m *GetACLReq) XXX_Size() int {
	return xxx_messageInfo_GetACLReq.Size(m)
}
func (m *GetACLReq) XXX_DiscardUnknown() {
	xxx_messageInfo_GetACLReq.DiscardUnknown(m)
}

var xxx_messageInfo_GetACLReq proto.InternalMessageInfo

func (m *GetACLReq) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

// Request to modify an ACL
// Results depend on the specific modification command.
type ModifyACLReq struct {
	Uuid                 string   `protobuf:"bytes,1,opt,name=uuid,proto3" json:"uuid,omitempty"`
	ACL                  []string `protobuf:"bytes,2,rep,name=ACL,proto3" json:"ACL,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ModifyACLReq) Reset()         { *m = ModifyACLReq{} }
func (m *ModifyACLReq) String() string { return proto.CompactTextString(m) }
func (*ModifyACLReq) ProtoMessage()    {}
func (*ModifyACLReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_acl_d5c9fbb52c9214a2, []int{2}
}
func (m *ModifyACLReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ModifyACLReq.Unmarshal(m, b)
}
func (m *ModifyACLReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ModifyACLReq.Marshal(b, m, deterministic)
}
func (dst *ModifyACLReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ModifyACLReq.Merge(dst, src)
}
func (m *ModifyACLReq) XXX_Size() int {
	return xxx_messageInfo_ModifyACLReq.Size(m)
}
func (m *ModifyACLReq) XXX_DiscardUnknown() {
	xxx_messageInfo_ModifyACLReq.DiscardUnknown(m)
}

var xxx_messageInfo_ModifyACLReq proto.InternalMessageInfo

func (m *ModifyACLReq) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *ModifyACLReq) GetACL() []string {
	if m != nil {
		return m.ACL
	}
	return nil
}

// Delete a principal's entry from the ACL
type DeleteACLReq struct {
	Uuid                 string   `protobuf:"bytes,1,opt,name=uuid,proto3" json:"uuid,omitempty"`
	Principal            string   `protobuf:"bytes,2,opt,name=principal,proto3" json:"principal,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *DeleteACLReq) Reset()         { *m = DeleteACLReq{} }
func (m *DeleteACLReq) String() string { return proto.CompactTextString(m) }
func (*DeleteACLReq) ProtoMessage()    {}
func (*DeleteACLReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_acl_d5c9fbb52c9214a2, []int{3}
}
func (m *DeleteACLReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_DeleteACLReq.Unmarshal(m, b)
}
func (m *DeleteACLReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_DeleteACLReq.Marshal(b, m, deterministic)
}
func (dst *DeleteACLReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_DeleteACLReq.Merge(dst, src)
}
func (m *DeleteACLReq) XXX_Size() int {
	return xxx_messageInfo_DeleteACLReq.Size(m)
}
func (m *DeleteACLReq) XXX_DiscardUnknown() {
	xxx_messageInfo_DeleteACLReq.DiscardUnknown(m)
}

var xxx_messageInfo_DeleteACLReq proto.InternalMessageInfo

func (m *DeleteACLReq) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *DeleteACLReq) GetPrincipal() string {
	if m != nil {
		return m.Principal
	}
	return ""
}

func init() {
	proto.RegisterType((*ACLResp)(nil), "mgmt.ACLResp")
	proto.RegisterType((*GetACLReq)(nil), "mgmt.GetACLReq")
	proto.RegisterType((*ModifyACLReq)(nil), "mgmt.ModifyACLReq")
	proto.RegisterType((*DeleteACLReq)(nil), "mgmt.DeleteACLReq")
}

func init() { proto.RegisterFile("acl.proto", fileDescriptor_acl_d5c9fbb52c9214a2) }

var fileDescriptor_acl_d5c9fbb52c9214a2 = []byte{
	// 199 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xe2, 0xe2, 0x4c, 0x4c, 0xce, 0xd1,
	0x2b, 0x28, 0xca, 0x2f, 0xc9, 0x17, 0x62, 0xc9, 0x4d, 0xcf, 0x2d, 0x51, 0x2a, 0xe4, 0x62, 0x77,
	0x74, 0xf6, 0x09, 0x4a, 0x2d, 0x2e, 0x10, 0x12, 0xe3, 0x62, 0x2b, 0x2e, 0x49, 0x2c, 0x29, 0x2d,
	0x96, 0x60, 0x54, 0x60, 0xd4, 0x60, 0x0d, 0x82, 0xf2, 0x84, 0x04, 0xb8, 0x98, 0x1d, 0x9d, 0x7d,
	0x24, 0x98, 0x14, 0x98, 0x35, 0x38, 0x83, 0x40, 0x4c, 0x21, 0x19, 0x2e, 0xce, 0xfc, 0xf2, 0xbc,
	0xd4, 0xa2, 0xd0, 0xe2, 0xd4, 0x22, 0x09, 0x66, 0x05, 0x46, 0x0d, 0xce, 0x20, 0x84, 0x80, 0x90,
	0x1c, 0x17, 0x17, 0x98, 0xe3, 0x5e, 0x94, 0x5f, 0x5a, 0x20, 0xc1, 0x02, 0x96, 0x46, 0x12, 0x51,
	0x92, 0xe7, 0xe2, 0x74, 0x4f, 0x2d, 0x01, 0xdb, 0x5a, 0x28, 0x24, 0xc4, 0xc5, 0x52, 0x5a, 0x9a,
	0x99, 0x02, 0xb6, 0x92, 0x33, 0x08, 0xcc, 0x56, 0x32, 0xe1, 0xe2, 0xf1, 0xcd, 0x4f, 0xc9, 0x4c,
	0xab, 0xc4, 0xad, 0x06, 0xd3, 0x51, 0x4a, 0x0e, 0x5c, 0x3c, 0x2e, 0xa9, 0x39, 0xa9, 0x25, 0xa9,
	0x78, 0x74, 0xc9, 0x70, 0x71, 0x16, 0x14, 0x65, 0xe6, 0x25, 0x67, 0x16, 0x24, 0xe6, 0x48, 0x30,
	0x41, 0x1c, 0x0e, 0x17, 0x48, 0x62, 0x03, 0x07, 0x8c, 0x31, 0x20, 0x00, 0x00, 0xff, 0xff, 0x83,
	0x76, 0x0d, 0x7e, 0x25, 0x01, 0x00, 0x00,
}
