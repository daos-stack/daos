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
	State                uint32   `protobuf:"varint,4,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SystemMember) Reset()         { *m = SystemMember{} }
func (m *SystemMember) String() string { return proto.CompactTextString(m) }
func (*SystemMember) ProtoMessage()    {}
func (*SystemMember) Descriptor() ([]byte, []int) {
	return fileDescriptor_system_cff7d0e57c7d1c1c, []int{0}
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

func (m *SystemMember) GetState() uint32 {
	if m != nil {
		return m.State
	}
	return 0
}

// SystemStopReq supplies system shutdown parameters.
type SystemStopReq struct {
	Prep                 bool     `protobuf:"varint,1,opt,name=prep,proto3" json:"prep,omitempty"`
	Kill                 bool     `protobuf:"varint,2,opt,name=kill,proto3" json:"kill,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SystemStopReq) Reset()         { *m = SystemStopReq{} }
func (m *SystemStopReq) String() string { return proto.CompactTextString(m) }
func (*SystemStopReq) ProtoMessage()    {}
func (*SystemStopReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_system_cff7d0e57c7d1c1c, []int{1}
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

func (m *SystemStopReq) GetPrep() bool {
	if m != nil {
		return m.Prep
	}
	return false
}

func (m *SystemStopReq) GetKill() bool {
	if m != nil {
		return m.Kill
	}
	return false
}

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
	return fileDescriptor_system_cff7d0e57c7d1c1c, []int{2}
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
	Rank                 uint32   `protobuf:"varint,1,opt,name=rank,proto3" json:"rank,omitempty"`
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
	return fileDescriptor_system_cff7d0e57c7d1c1c, []int{2, 0}
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

func (m *SystemStopResp_Result) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
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
	return fileDescriptor_system_cff7d0e57c7d1c1c, []int{3}
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
	return fileDescriptor_system_cff7d0e57c7d1c1c, []int{4}
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

func init() { proto.RegisterFile("system.proto", fileDescriptor_system_cff7d0e57c7d1c1c) }

var fileDescriptor_system_cff7d0e57c7d1c1c = []byte{
	// 275 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x6c, 0x91, 0xc1, 0x4a, 0xc3, 0x40,
	0x10, 0x86, 0x59, 0x53, 0x93, 0x74, 0x6c, 0x45, 0x97, 0x5a, 0x96, 0x9e, 0x42, 0x4e, 0x01, 0x21,
	0x07, 0x15, 0x7c, 0x01, 0xaf, 0x1e, 0x9c, 0xbe, 0x40, 0xd3, 0x64, 0x90, 0xd0, 0xa4, 0xd9, 0xee,
	0x6e, 0x0e, 0x7d, 0x23, 0x1f, 0x53, 0x76, 0xd2, 0x48, 0x84, 0xde, 0xfe, 0xf9, 0xf8, 0x87, 0x7f,
	0xfe, 0x5d, 0x58, 0xd8, 0xb3, 0x75, 0xd4, 0xe6, 0xda, 0x74, 0xae, 0x93, 0x41, 0xe9, 0x9a, 0x74,
	0x07, 0x8b, 0x2d, 0xc3, 0x4f, 0x6a, 0xf7, 0x64, 0xa4, 0x84, 0x59, 0x51, 0x55, 0x46, 0x89, 0x44,
	0x64, 0x73, 0x64, 0xed, 0x59, 0xdf, 0xd7, 0x95, 0xba, 0x19, 0x98, 0xd7, 0x9e, 0x99, 0xe2, 0x78,
	0x50, 0x41, 0x22, 0xb2, 0x25, 0xb2, 0x96, 0x2b, 0xb8, 0xb5, 0xae, 0x70, 0xa4, 0x66, 0x0c, 0x87,
	0x21, 0x7d, 0x87, 0xe5, 0x90, 0xb0, 0x75, 0x9d, 0x46, 0x3a, 0xf9, 0x55, 0x6d, 0x48, 0x73, 0x44,
	0x8c, 0xac, 0x3d, 0x3b, 0xd4, 0x4d, 0xc3, 0x11, 0x31, 0xb2, 0x4e, 0x7f, 0x04, 0xdc, 0x4f, 0x37,
	0xad, 0x96, 0x6f, 0x10, 0x19, 0xb2, 0x7d, 0xe3, 0xac, 0x12, 0x49, 0x90, 0xdd, 0xbd, 0x6c, 0xf2,
	0xd2, 0x35, 0xf9, 0x7f, 0x57, 0x8e, 0x6c, 0xc1, 0xd1, 0xba, 0xd9, 0x41, 0x38, 0xa0, 0xbf, 0xab,
	0xc5, 0xe4, 0xea, 0x35, 0x84, 0x45, 0xe9, 0xea, 0xee, 0x78, 0xe9, 0x77, 0x99, 0xa4, 0x82, 0x88,
	0x8c, 0xe9, 0x0c, 0x55, 0x5c, 0x32, 0xc6, 0x71, 0x94, 0x0f, 0x10, 0xb4, 0xf6, 0x9b, 0x5b, 0xce,
	0xd1, 0xcb, 0x74, 0x0d, 0xab, 0xe9, 0x2b, 0x7e, 0xf5, 0x64, 0xce, 0x48, 0xa7, 0xf4, 0x03, 0x9e,
	0xae, 0x70, 0xab, 0xe5, 0x33, 0x44, 0x2d, 0xa3, 0xb1, 0xc8, 0xe3, 0xa4, 0xc8, 0x60, 0xc6, 0xd1,
	0xb1, 0x0f, 0xf9, 0xbf, 0x5e, 0x7f, 0x03, 0x00, 0x00, 0xff, 0xff, 0xaa, 0x76, 0x14, 0x16, 0xbf,
	0x01, 0x00, 0x00,
}
