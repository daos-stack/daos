// Code generated by protoc-gen-go. DO NOT EDIT.
// source: system.proto

package ctl

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

// SystemMember refers to a data-plane instance that is a member of DAOS
// system running on host with the control-plane listening at "Addr".
type SystemMember struct {
	Addr                 string   `protobuf:"bytes,1,opt,name=addr,proto3" json:"addr,omitempty"`
	Uuid                 string   `protobuf:"bytes,2,opt,name=uuid,proto3" json:"uuid,omitempty"`
	Rank                 uint32   `protobuf:"varint,3,opt,name=rank,proto3" json:"rank,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SystemMember) Reset()         { *m = SystemMember{} }
func (m *SystemMember) String() string { return proto.CompactTextString(m) }
func (*SystemMember) ProtoMessage()    {}
func (*SystemMember) Descriptor() ([]byte, []int) {
	return fileDescriptor_system_76bbc6892e2a9330, []int{0}
}
func (m *SystemMember) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SystemMember.Unmarshal(m, b)
}
func (m *SystemMember) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SystemMember.Marshal(b, m, deterministic)
}
func (dst *SystemMember) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SystemMember.Merge(dst, src)
}
func (m *SystemMember) XXX_Size() int {
	return xxx_messageInfo_SystemMember.Size(m)
}
func (m *SystemMember) XXX_DiscardUnknown() {
	xxx_messageInfo_SystemMember.DiscardUnknown(m)
}

var xxx_messageInfo_SystemMember proto.InternalMessageInfo

func (m *SystemMember) GetAddr() string {
	if m != nil {
		return m.Addr
	}
	return ""
}

func (m *SystemMember) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *SystemMember) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

// SystemStopReq supplies system shutdown parameters.
type SystemStopReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SystemStopReq) Reset()         { *m = SystemStopReq{} }
func (m *SystemStopReq) String() string { return proto.CompactTextString(m) }
func (*SystemStopReq) ProtoMessage()    {}
func (*SystemStopReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_system_76bbc6892e2a9330, []int{1}
}
func (m *SystemStopReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SystemStopReq.Unmarshal(m, b)
}
func (m *SystemStopReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SystemStopReq.Marshal(b, m, deterministic)
}
func (dst *SystemStopReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SystemStopReq.Merge(dst, src)
}
func (m *SystemStopReq) XXX_Size() int {
	return xxx_messageInfo_SystemStopReq.Size(m)
}
func (m *SystemStopReq) XXX_DiscardUnknown() {
	xxx_messageInfo_SystemStopReq.DiscardUnknown(m)
}

var xxx_messageInfo_SystemStopReq proto.InternalMessageInfo

// SystemStopResp returns status of shutdown attempt and results
// of attempts to stop system members.
type SystemStopResp struct {
	Results              []*SystemStopResp_Result `protobuf:"bytes,1,rep,name=results,proto3" json:"results,omitempty"`
	XXX_NoUnkeyedLiteral struct{}                 `json:"-"`
	XXX_unrecognized     []byte                   `json:"-"`
	XXX_sizecache        int32                    `json:"-"`
}

func (m *SystemStopResp) Reset()         { *m = SystemStopResp{} }
func (m *SystemStopResp) String() string { return proto.CompactTextString(m) }
func (*SystemStopResp) ProtoMessage()    {}
func (*SystemStopResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_system_76bbc6892e2a9330, []int{2}
}
func (m *SystemStopResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SystemStopResp.Unmarshal(m, b)
}
func (m *SystemStopResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SystemStopResp.Marshal(b, m, deterministic)
}
func (dst *SystemStopResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SystemStopResp.Merge(dst, src)
}
func (m *SystemStopResp) XXX_Size() int {
	return xxx_messageInfo_SystemStopResp.Size(m)
}
func (m *SystemStopResp) XXX_DiscardUnknown() {
	xxx_messageInfo_SystemStopResp.DiscardUnknown(m)
}

var xxx_messageInfo_SystemStopResp proto.InternalMessageInfo

func (m *SystemStopResp) GetResults() []*SystemStopResp_Result {
	if m != nil {
		return m.Results
	}
	return nil
}

type SystemStopResp_Result struct {
	Id                   string   `protobuf:"bytes,1,opt,name=id,proto3" json:"id,omitempty"`
	Action               string   `protobuf:"bytes,2,opt,name=action,proto3" json:"action,omitempty"`
	Errored              bool     `protobuf:"varint,3,opt,name=errored,proto3" json:"errored,omitempty"`
	Msg                  string   `protobuf:"bytes,4,opt,name=msg,proto3" json:"msg,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SystemStopResp_Result) Reset()         { *m = SystemStopResp_Result{} }
func (m *SystemStopResp_Result) String() string { return proto.CompactTextString(m) }
func (*SystemStopResp_Result) ProtoMessage()    {}
func (*SystemStopResp_Result) Descriptor() ([]byte, []int) {
	return fileDescriptor_system_76bbc6892e2a9330, []int{2, 0}
}
func (m *SystemStopResp_Result) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SystemStopResp_Result.Unmarshal(m, b)
}
func (m *SystemStopResp_Result) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SystemStopResp_Result.Marshal(b, m, deterministic)
}
func (dst *SystemStopResp_Result) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SystemStopResp_Result.Merge(dst, src)
}
func (m *SystemStopResp_Result) XXX_Size() int {
	return xxx_messageInfo_SystemStopResp_Result.Size(m)
}
func (m *SystemStopResp_Result) XXX_DiscardUnknown() {
	xxx_messageInfo_SystemStopResp_Result.DiscardUnknown(m)
}

var xxx_messageInfo_SystemStopResp_Result proto.InternalMessageInfo

func (m *SystemStopResp_Result) GetId() string {
	if m != nil {
		return m.Id
	}
	return ""
}

func (m *SystemStopResp_Result) GetAction() string {
	if m != nil {
		return m.Action
	}
	return ""
}

func (m *SystemStopResp_Result) GetErrored() bool {
	if m != nil {
		return m.Errored
	}
	return false
}

func (m *SystemStopResp_Result) GetMsg() string {
	if m != nil {
		return m.Msg
	}
	return ""
}

// SystemMemberQueryReq supplies system query parameters.
type SystemMemberQueryReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SystemMemberQueryReq) Reset()         { *m = SystemMemberQueryReq{} }
func (m *SystemMemberQueryReq) String() string { return proto.CompactTextString(m) }
func (*SystemMemberQueryReq) ProtoMessage()    {}
func (*SystemMemberQueryReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_system_76bbc6892e2a9330, []int{3}
}
func (m *SystemMemberQueryReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SystemMemberQueryReq.Unmarshal(m, b)
}
func (m *SystemMemberQueryReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SystemMemberQueryReq.Marshal(b, m, deterministic)
}
func (dst *SystemMemberQueryReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SystemMemberQueryReq.Merge(dst, src)
}
func (m *SystemMemberQueryReq) XXX_Size() int {
	return xxx_messageInfo_SystemMemberQueryReq.Size(m)
}
func (m *SystemMemberQueryReq) XXX_DiscardUnknown() {
	xxx_messageInfo_SystemMemberQueryReq.DiscardUnknown(m)
}

var xxx_messageInfo_SystemMemberQueryReq proto.InternalMessageInfo

// SystemMemberQueryResp returns active system members.
type SystemMemberQueryResp struct {
	Members              []*SystemMember `protobuf:"bytes,1,rep,name=members,proto3" json:"members,omitempty"`
	XXX_NoUnkeyedLiteral struct{}        `json:"-"`
	XXX_unrecognized     []byte          `json:"-"`
	XXX_sizecache        int32           `json:"-"`
}

func (m *SystemMemberQueryResp) Reset()         { *m = SystemMemberQueryResp{} }
func (m *SystemMemberQueryResp) String() string { return proto.CompactTextString(m) }
func (*SystemMemberQueryResp) ProtoMessage()    {}
func (*SystemMemberQueryResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_system_76bbc6892e2a9330, []int{4}
}
func (m *SystemMemberQueryResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SystemMemberQueryResp.Unmarshal(m, b)
}
func (m *SystemMemberQueryResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SystemMemberQueryResp.Marshal(b, m, deterministic)
}
func (dst *SystemMemberQueryResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SystemMemberQueryResp.Merge(dst, src)
}
func (m *SystemMemberQueryResp) XXX_Size() int {
	return xxx_messageInfo_SystemMemberQueryResp.Size(m)
}
func (m *SystemMemberQueryResp) XXX_DiscardUnknown() {
	xxx_messageInfo_SystemMemberQueryResp.DiscardUnknown(m)
}

var xxx_messageInfo_SystemMemberQueryResp proto.InternalMessageInfo

func (m *SystemMemberQueryResp) GetMembers() []*SystemMember {
	if m != nil {
		return m.Members
	}
	return nil
}

func init() {
	proto.RegisterType((*SystemMember)(nil), "ctl.SystemMember")
	proto.RegisterType((*SystemStopReq)(nil), "ctl.SystemStopReq")
	proto.RegisterType((*SystemStopResp)(nil), "ctl.SystemStopResp")
	proto.RegisterType((*SystemStopResp_Result)(nil), "ctl.SystemStopResp.Result")
	proto.RegisterType((*SystemMemberQueryReq)(nil), "ctl.SystemMemberQueryReq")
	proto.RegisterType((*SystemMemberQueryResp)(nil), "ctl.SystemMemberQueryResp")
}

func init() { proto.RegisterFile("system.proto", fileDescriptor_system_76bbc6892e2a9330) }

var fileDescriptor_system_76bbc6892e2a9330 = []byte{
	// 249 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x6c, 0x90, 0xc1, 0x4a, 0xc3, 0x40,
	0x10, 0x86, 0xd9, 0xa4, 0x24, 0x3a, 0xb6, 0x55, 0x07, 0x2d, 0x4b, 0x4f, 0x21, 0xa7, 0x80, 0x90,
	0x83, 0xfa, 0x08, 0x9e, 0x04, 0x0f, 0x6e, 0xaf, 0x5e, 0xd2, 0xec, 0x22, 0xc1, 0xa4, 0x1b, 0x67,
	0x37, 0x87, 0xbe, 0x8f, 0x0f, 0x2a, 0x3b, 0x49, 0x20, 0x82, 0xb7, 0x7f, 0x3e, 0x3e, 0x86, 0x7f,
	0x06, 0xd6, 0xee, 0xec, 0xbc, 0xe9, 0xca, 0x9e, 0xac, 0xb7, 0x18, 0xd7, 0xbe, 0xcd, 0x5f, 0x61,
	0x7d, 0x60, 0xf8, 0x66, 0xba, 0xa3, 0x21, 0x44, 0x58, 0x55, 0x5a, 0x93, 0x14, 0x99, 0x28, 0x2e,
	0x15, 0xe7, 0xc0, 0x86, 0xa1, 0xd1, 0x32, 0x1a, 0x59, 0xc8, 0x81, 0x51, 0x75, 0xfa, 0x92, 0x71,
	0x26, 0x8a, 0x8d, 0xe2, 0x9c, 0x5f, 0xc3, 0x66, 0xdc, 0x75, 0xf0, 0xb6, 0x57, 0xe6, 0x3b, 0xff,
	0x11, 0xb0, 0x5d, 0x12, 0xd7, 0xe3, 0x33, 0xa4, 0x64, 0xdc, 0xd0, 0x7a, 0x27, 0x45, 0x16, 0x17,
	0x57, 0x8f, 0xfb, 0xb2, 0xf6, 0x6d, 0xf9, 0xd7, 0x2a, 0x15, 0x2b, 0x6a, 0x56, 0xf7, 0x1f, 0x90,
	0x8c, 0x08, 0xb7, 0x10, 0x35, 0x7a, 0x6a, 0x17, 0x35, 0x1a, 0x77, 0x90, 0x54, 0xb5, 0x6f, 0xec,
	0x69, 0x6a, 0x37, 0x4d, 0x28, 0x21, 0x35, 0x44, 0x96, 0x8c, 0xe6, 0x8a, 0x17, 0x6a, 0x1e, 0xf1,
	0x06, 0xe2, 0xce, 0x7d, 0xca, 0x15, 0xeb, 0x21, 0xe6, 0x3b, 0xb8, 0x5b, 0xfe, 0xe0, 0x7d, 0x30,
	0x74, 0x0e, 0xf5, 0x5f, 0xe0, 0xfe, 0x1f, 0xee, 0x7a, 0x7c, 0x80, 0xb4, 0x63, 0x34, 0x1f, 0x71,
	0xbb, 0x38, 0x62, 0x94, 0xd5, 0x6c, 0x1c, 0x13, 0xfe, 0xf6, 0xd3, 0x6f, 0x00, 0x00, 0x00, 0xff,
	0xff, 0xe6, 0x1d, 0xd5, 0x21, 0x7d, 0x01, 0x00, 0x00,
}
