// Code generated by protoc-gen-go. DO NOT EDIT.
// source: srv.proto

package mgmt

import (
	fmt "fmt"
	proto "github.com/golang/protobuf/proto"
	math "math"
)

// Reference imports to suppress errors if they are not otherwise used.
var _ = proto.Marshal
var _ = fmt.Errorf
var _ = math.Inf

// This is a compile-time assertion to ensure that this generated file
// is compatible with the proto package it is being compiled against.
// A compilation error at this line likely means your copy of the
// proto package needs to be updated.
const _ = proto.ProtoPackageIsVersion3 // please upgrade the proto package

type JoinResp_State int32

const (
	JoinResp_IN  JoinResp_State = 0
	JoinResp_OUT JoinResp_State = 1
)

var JoinResp_State_name = map[int32]string{
	0: "IN",
	1: "OUT",
}

var JoinResp_State_value = map[string]int32{
	"IN":  0,
	"OUT": 1,
}

func (x JoinResp_State) String() string {
	return proto.EnumName(JoinResp_State_name, int32(x))
}

func (JoinResp_State) EnumDescriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{2, 0}
}

// Generic response just containing DER from IO server.
type DaosResp struct {
	Status               int32    `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *DaosResp) Reset()         { *m = DaosResp{} }
func (m *DaosResp) String() string { return proto.CompactTextString(m) }
func (*DaosResp) ProtoMessage()    {}
func (*DaosResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{0}
}

func (m *DaosResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_DaosResp.Unmarshal(m, b)
}
func (m *DaosResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_DaosResp.Marshal(b, m, deterministic)
}
func (m *DaosResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_DaosResp.Merge(m, src)
}
func (m *DaosResp) XXX_Size() int {
	return xxx_messageInfo_DaosResp.Size(m)
}
func (m *DaosResp) XXX_DiscardUnknown() {
	xxx_messageInfo_DaosResp.DiscardUnknown(m)
}

var xxx_messageInfo_DaosResp proto.InternalMessageInfo

func (m *DaosResp) GetStatus() int32 {
	if m != nil {
		return m.Status
	}
	return 0
}

type JoinReq struct {
	Uuid                 string   `protobuf:"bytes,1,opt,name=uuid,proto3" json:"uuid,omitempty"`
	Rank                 uint32   `protobuf:"varint,2,opt,name=rank,proto3" json:"rank,omitempty"`
	Uri                  string   `protobuf:"bytes,3,opt,name=uri,proto3" json:"uri,omitempty"`
	Nctxs                uint32   `protobuf:"varint,4,opt,name=nctxs,proto3" json:"nctxs,omitempty"`
	Addr                 string   `protobuf:"bytes,5,opt,name=addr,proto3" json:"addr,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *JoinReq) Reset()         { *m = JoinReq{} }
func (m *JoinReq) String() string { return proto.CompactTextString(m) }
func (*JoinReq) ProtoMessage()    {}
func (*JoinReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{1}
}

func (m *JoinReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_JoinReq.Unmarshal(m, b)
}
func (m *JoinReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_JoinReq.Marshal(b, m, deterministic)
}
func (m *JoinReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_JoinReq.Merge(m, src)
}
func (m *JoinReq) XXX_Size() int {
	return xxx_messageInfo_JoinReq.Size(m)
}
func (m *JoinReq) XXX_DiscardUnknown() {
	xxx_messageInfo_JoinReq.DiscardUnknown(m)
}

var xxx_messageInfo_JoinReq proto.InternalMessageInfo

func (m *JoinReq) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *JoinReq) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

func (m *JoinReq) GetUri() string {
	if m != nil {
		return m.Uri
	}
	return ""
}

func (m *JoinReq) GetNctxs() uint32 {
	if m != nil {
		return m.Nctxs
	}
	return 0
}

func (m *JoinReq) GetAddr() string {
	if m != nil {
		return m.Addr
	}
	return ""
}

type JoinResp struct {
	Status               int32          `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	Rank                 uint32         `protobuf:"varint,2,opt,name=rank,proto3" json:"rank,omitempty"`
	State                JoinResp_State `protobuf:"varint,3,opt,name=state,proto3,enum=mgmt.JoinResp_State" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *JoinResp) Reset()         { *m = JoinResp{} }
func (m *JoinResp) String() string { return proto.CompactTextString(m) }
func (*JoinResp) ProtoMessage()    {}
func (*JoinResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{2}
}

func (m *JoinResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_JoinResp.Unmarshal(m, b)
}
func (m *JoinResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_JoinResp.Marshal(b, m, deterministic)
}
func (m *JoinResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_JoinResp.Merge(m, src)
}
func (m *JoinResp) XXX_Size() int {
	return xxx_messageInfo_JoinResp.Size(m)
}
func (m *JoinResp) XXX_DiscardUnknown() {
	xxx_messageInfo_JoinResp.DiscardUnknown(m)
}

var xxx_messageInfo_JoinResp proto.InternalMessageInfo

func (m *JoinResp) GetStatus() int32 {
	if m != nil {
		return m.Status
	}
	return 0
}

func (m *JoinResp) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

func (m *JoinResp) GetState() JoinResp_State {
	if m != nil {
		return m.State
	}
	return JoinResp_IN
}

type LeaderQueryReq struct {
	System               string   `protobuf:"bytes,1,opt,name=system,proto3" json:"system,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *LeaderQueryReq) Reset()         { *m = LeaderQueryReq{} }
func (m *LeaderQueryReq) String() string { return proto.CompactTextString(m) }
func (*LeaderQueryReq) ProtoMessage()    {}
func (*LeaderQueryReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{3}
}

func (m *LeaderQueryReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_LeaderQueryReq.Unmarshal(m, b)
}
func (m *LeaderQueryReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_LeaderQueryReq.Marshal(b, m, deterministic)
}
func (m *LeaderQueryReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_LeaderQueryReq.Merge(m, src)
}
func (m *LeaderQueryReq) XXX_Size() int {
	return xxx_messageInfo_LeaderQueryReq.Size(m)
}
func (m *LeaderQueryReq) XXX_DiscardUnknown() {
	xxx_messageInfo_LeaderQueryReq.DiscardUnknown(m)
}

var xxx_messageInfo_LeaderQueryReq proto.InternalMessageInfo

func (m *LeaderQueryReq) GetSystem() string {
	if m != nil {
		return m.System
	}
	return ""
}

type LeaderQueryResp struct {
	CurrentLeader        string   `protobuf:"bytes,1,opt,name=currentLeader,proto3" json:"currentLeader,omitempty"`
	Replicas             []string `protobuf:"bytes,2,rep,name=replicas,proto3" json:"replicas,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *LeaderQueryResp) Reset()         { *m = LeaderQueryResp{} }
func (m *LeaderQueryResp) String() string { return proto.CompactTextString(m) }
func (*LeaderQueryResp) ProtoMessage()    {}
func (*LeaderQueryResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{4}
}

func (m *LeaderQueryResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_LeaderQueryResp.Unmarshal(m, b)
}
func (m *LeaderQueryResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_LeaderQueryResp.Marshal(b, m, deterministic)
}
func (m *LeaderQueryResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_LeaderQueryResp.Merge(m, src)
}
func (m *LeaderQueryResp) XXX_Size() int {
	return xxx_messageInfo_LeaderQueryResp.Size(m)
}
func (m *LeaderQueryResp) XXX_DiscardUnknown() {
	xxx_messageInfo_LeaderQueryResp.DiscardUnknown(m)
}

var xxx_messageInfo_LeaderQueryResp proto.InternalMessageInfo

func (m *LeaderQueryResp) GetCurrentLeader() string {
	if m != nil {
		return m.CurrentLeader
	}
	return ""
}

func (m *LeaderQueryResp) GetReplicas() []string {
	if m != nil {
		return m.Replicas
	}
	return nil
}

type GetAttachInfoReq struct {
	Sys                  string   `protobuf:"bytes,1,opt,name=sys,proto3" json:"sys,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *GetAttachInfoReq) Reset()         { *m = GetAttachInfoReq{} }
func (m *GetAttachInfoReq) String() string { return proto.CompactTextString(m) }
func (*GetAttachInfoReq) ProtoMessage()    {}
func (*GetAttachInfoReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{5}
}

func (m *GetAttachInfoReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_GetAttachInfoReq.Unmarshal(m, b)
}
func (m *GetAttachInfoReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_GetAttachInfoReq.Marshal(b, m, deterministic)
}
func (m *GetAttachInfoReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_GetAttachInfoReq.Merge(m, src)
}
func (m *GetAttachInfoReq) XXX_Size() int {
	return xxx_messageInfo_GetAttachInfoReq.Size(m)
}
func (m *GetAttachInfoReq) XXX_DiscardUnknown() {
	xxx_messageInfo_GetAttachInfoReq.DiscardUnknown(m)
}

var xxx_messageInfo_GetAttachInfoReq proto.InternalMessageInfo

func (m *GetAttachInfoReq) GetSys() string {
	if m != nil {
		return m.Sys
	}
	return ""
}

type GetAttachInfoResp struct {
	Status               int32                    `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	Psrs                 []*GetAttachInfoResp_Psr `protobuf:"bytes,2,rep,name=psrs,proto3" json:"psrs,omitempty"`
	XXX_NoUnkeyedLiteral struct{}                 `json:"-"`
	XXX_unrecognized     []byte                   `json:"-"`
	XXX_sizecache        int32                    `json:"-"`
}

func (m *GetAttachInfoResp) Reset()         { *m = GetAttachInfoResp{} }
func (m *GetAttachInfoResp) String() string { return proto.CompactTextString(m) }
func (*GetAttachInfoResp) ProtoMessage()    {}
func (*GetAttachInfoResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{6}
}

func (m *GetAttachInfoResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_GetAttachInfoResp.Unmarshal(m, b)
}
func (m *GetAttachInfoResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_GetAttachInfoResp.Marshal(b, m, deterministic)
}
func (m *GetAttachInfoResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_GetAttachInfoResp.Merge(m, src)
}
func (m *GetAttachInfoResp) XXX_Size() int {
	return xxx_messageInfo_GetAttachInfoResp.Size(m)
}
func (m *GetAttachInfoResp) XXX_DiscardUnknown() {
	xxx_messageInfo_GetAttachInfoResp.DiscardUnknown(m)
}

var xxx_messageInfo_GetAttachInfoResp proto.InternalMessageInfo

func (m *GetAttachInfoResp) GetStatus() int32 {
	if m != nil {
		return m.Status
	}
	return 0
}

func (m *GetAttachInfoResp) GetPsrs() []*GetAttachInfoResp_Psr {
	if m != nil {
		return m.Psrs
	}
	return nil
}

type GetAttachInfoResp_Psr struct {
	Rank                 uint32   `protobuf:"varint,1,opt,name=rank,proto3" json:"rank,omitempty"`
	Uri                  string   `protobuf:"bytes,2,opt,name=uri,proto3" json:"uri,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *GetAttachInfoResp_Psr) Reset()         { *m = GetAttachInfoResp_Psr{} }
func (m *GetAttachInfoResp_Psr) String() string { return proto.CompactTextString(m) }
func (*GetAttachInfoResp_Psr) ProtoMessage()    {}
func (*GetAttachInfoResp_Psr) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{6, 0}
}

func (m *GetAttachInfoResp_Psr) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_GetAttachInfoResp_Psr.Unmarshal(m, b)
}
func (m *GetAttachInfoResp_Psr) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_GetAttachInfoResp_Psr.Marshal(b, m, deterministic)
}
func (m *GetAttachInfoResp_Psr) XXX_Merge(src proto.Message) {
	xxx_messageInfo_GetAttachInfoResp_Psr.Merge(m, src)
}
func (m *GetAttachInfoResp_Psr) XXX_Size() int {
	return xxx_messageInfo_GetAttachInfoResp_Psr.Size(m)
}
func (m *GetAttachInfoResp_Psr) XXX_DiscardUnknown() {
	xxx_messageInfo_GetAttachInfoResp_Psr.DiscardUnknown(m)
}

var xxx_messageInfo_GetAttachInfoResp_Psr proto.InternalMessageInfo

func (m *GetAttachInfoResp_Psr) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

func (m *GetAttachInfoResp_Psr) GetUri() string {
	if m != nil {
		return m.Uri
	}
	return ""
}

type PrepShutdownReq struct {
	Rank                 uint32   `protobuf:"varint,1,opt,name=rank,proto3" json:"rank,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *PrepShutdownReq) Reset()         { *m = PrepShutdownReq{} }
func (m *PrepShutdownReq) String() string { return proto.CompactTextString(m) }
func (*PrepShutdownReq) ProtoMessage()    {}
func (*PrepShutdownReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{7}
}

func (m *PrepShutdownReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PrepShutdownReq.Unmarshal(m, b)
}
func (m *PrepShutdownReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PrepShutdownReq.Marshal(b, m, deterministic)
}
func (m *PrepShutdownReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PrepShutdownReq.Merge(m, src)
}
func (m *PrepShutdownReq) XXX_Size() int {
	return xxx_messageInfo_PrepShutdownReq.Size(m)
}
func (m *PrepShutdownReq) XXX_DiscardUnknown() {
	xxx_messageInfo_PrepShutdownReq.DiscardUnknown(m)
}

var xxx_messageInfo_PrepShutdownReq proto.InternalMessageInfo

func (m *PrepShutdownReq) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

type PingRankReq struct {
	Rank                 uint32   `protobuf:"varint,1,opt,name=rank,proto3" json:"rank,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *PingRankReq) Reset()         { *m = PingRankReq{} }
func (m *PingRankReq) String() string { return proto.CompactTextString(m) }
func (*PingRankReq) ProtoMessage()    {}
func (*PingRankReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{8}
}

func (m *PingRankReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PingRankReq.Unmarshal(m, b)
}
func (m *PingRankReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PingRankReq.Marshal(b, m, deterministic)
}
func (m *PingRankReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PingRankReq.Merge(m, src)
}
func (m *PingRankReq) XXX_Size() int {
	return xxx_messageInfo_PingRankReq.Size(m)
}
func (m *PingRankReq) XXX_DiscardUnknown() {
	xxx_messageInfo_PingRankReq.DiscardUnknown(m)
}

var xxx_messageInfo_PingRankReq proto.InternalMessageInfo

func (m *PingRankReq) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

type SetRankReq struct {
	Rank                 uint32   `protobuf:"varint,1,opt,name=rank,proto3" json:"rank,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SetRankReq) Reset()         { *m = SetRankReq{} }
func (m *SetRankReq) String() string { return proto.CompactTextString(m) }
func (*SetRankReq) ProtoMessage()    {}
func (*SetRankReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{9}
}

func (m *SetRankReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SetRankReq.Unmarshal(m, b)
}
func (m *SetRankReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SetRankReq.Marshal(b, m, deterministic)
}
func (m *SetRankReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SetRankReq.Merge(m, src)
}
func (m *SetRankReq) XXX_Size() int {
	return xxx_messageInfo_SetRankReq.Size(m)
}
func (m *SetRankReq) XXX_DiscardUnknown() {
	xxx_messageInfo_SetRankReq.DiscardUnknown(m)
}

var xxx_messageInfo_SetRankReq proto.InternalMessageInfo

func (m *SetRankReq) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

type CreateMsReq struct {
	Bootstrap            bool     `protobuf:"varint,1,opt,name=bootstrap,proto3" json:"bootstrap,omitempty"`
	Uuid                 string   `protobuf:"bytes,2,opt,name=uuid,proto3" json:"uuid,omitempty"`
	Addr                 string   `protobuf:"bytes,3,opt,name=addr,proto3" json:"addr,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *CreateMsReq) Reset()         { *m = CreateMsReq{} }
func (m *CreateMsReq) String() string { return proto.CompactTextString(m) }
func (*CreateMsReq) ProtoMessage()    {}
func (*CreateMsReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{10}
}

func (m *CreateMsReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_CreateMsReq.Unmarshal(m, b)
}
func (m *CreateMsReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_CreateMsReq.Marshal(b, m, deterministic)
}
func (m *CreateMsReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_CreateMsReq.Merge(m, src)
}
func (m *CreateMsReq) XXX_Size() int {
	return xxx_messageInfo_CreateMsReq.Size(m)
}
func (m *CreateMsReq) XXX_DiscardUnknown() {
	xxx_messageInfo_CreateMsReq.DiscardUnknown(m)
}

var xxx_messageInfo_CreateMsReq proto.InternalMessageInfo

func (m *CreateMsReq) GetBootstrap() bool {
	if m != nil {
		return m.Bootstrap
	}
	return false
}

func (m *CreateMsReq) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *CreateMsReq) GetAddr() string {
	if m != nil {
		return m.Addr
	}
	return ""
}

func init() {
	proto.RegisterEnum("mgmt.JoinResp_State", JoinResp_State_name, JoinResp_State_value)
	proto.RegisterType((*DaosResp)(nil), "mgmt.DaosResp")
	proto.RegisterType((*JoinReq)(nil), "mgmt.JoinReq")
	proto.RegisterType((*JoinResp)(nil), "mgmt.JoinResp")
	proto.RegisterType((*LeaderQueryReq)(nil), "mgmt.LeaderQueryReq")
	proto.RegisterType((*LeaderQueryResp)(nil), "mgmt.LeaderQueryResp")
	proto.RegisterType((*GetAttachInfoReq)(nil), "mgmt.GetAttachInfoReq")
	proto.RegisterType((*GetAttachInfoResp)(nil), "mgmt.GetAttachInfoResp")
	proto.RegisterType((*GetAttachInfoResp_Psr)(nil), "mgmt.GetAttachInfoResp.Psr")
	proto.RegisterType((*PrepShutdownReq)(nil), "mgmt.PrepShutdownReq")
	proto.RegisterType((*PingRankReq)(nil), "mgmt.PingRankReq")
	proto.RegisterType((*SetRankReq)(nil), "mgmt.SetRankReq")
	proto.RegisterType((*CreateMsReq)(nil), "mgmt.CreateMsReq")
}

func init() { proto.RegisterFile("srv.proto", fileDescriptor_2bbe8325d22c1a26) }

var fileDescriptor_2bbe8325d22c1a26 = []byte{
	// 414 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x7c, 0x93, 0xeb, 0x6a, 0xd4, 0x40,
	0x14, 0x80, 0x4d, 0xb2, 0xd9, 0x6e, 0xce, 0xd2, 0x36, 0x0e, 0x45, 0x42, 0xf5, 0x47, 0x1c, 0x2a,
	0x04, 0x85, 0x08, 0xf5, 0x09, 0x44, 0x41, 0x2a, 0x5e, 0xe2, 0x44, 0x1f, 0x60, 0x9a, 0x8c, 0x6d,
	0xa8, 0xc9, 0xc4, 0x73, 0x26, 0xea, 0x82, 0x2f, 0xe0, 0x5b, 0xcb, 0x4c, 0xd2, 0x94, 0xed, 0xed,
	0xdf, 0xb9, 0x7c, 0xe7, 0x3a, 0x67, 0x20, 0x22, 0xfc, 0x95, 0xf7, 0xa8, 0x8d, 0x66, 0x8b, 0xf6,
	0xac, 0x35, 0x9c, 0xc3, 0xea, 0xad, 0xd4, 0x24, 0x14, 0xf5, 0xec, 0x11, 0x2c, 0xc9, 0x48, 0x33,
	0x50, 0xe2, 0xa5, 0x5e, 0x16, 0x8a, 0x49, 0xe3, 0x2d, 0xec, 0xbc, 0xd7, 0x4d, 0x27, 0xd4, 0x4f,
	0xc6, 0x60, 0x31, 0x0c, 0x4d, 0xed, 0x80, 0x48, 0x38, 0xd9, 0xda, 0x50, 0x76, 0x17, 0x89, 0x9f,
	0x7a, 0xd9, 0xae, 0x70, 0x32, 0x8b, 0x21, 0x18, 0xb0, 0x49, 0x02, 0x87, 0x59, 0x91, 0x1d, 0x40,
	0xd8, 0x55, 0xe6, 0x0f, 0x25, 0x0b, 0x87, 0x8d, 0x8a, 0x8d, 0x95, 0x75, 0x8d, 0x49, 0x38, 0xe6,
	0xb3, 0x32, 0xff, 0x0b, 0xab, 0xb1, 0xdc, 0xdd, 0x2d, 0xdd, 0x5a, 0xf3, 0x39, 0x84, 0xd6, 0xab,
	0x5c, 0xd5, 0xbd, 0xe3, 0x83, 0xdc, 0x0e, 0x98, 0x5f, 0xa6, 0xca, 0x4b, 0xeb, 0x13, 0x23, 0xc2,
	0x13, 0x08, 0x9d, 0xce, 0x96, 0xe0, 0x9f, 0x7c, 0x8a, 0x1f, 0xb0, 0x1d, 0x08, 0x3e, 0x7f, 0xfb,
	0x1a, 0x7b, 0x3c, 0x83, 0xbd, 0x0f, 0x4a, 0xd6, 0x0a, 0xbf, 0x0c, 0x0a, 0x37, 0x76, 0x66, 0xdb,
	0xc3, 0x86, 0x8c, 0x6a, 0xa7, 0xa9, 0x27, 0x8d, 0x97, 0xb0, 0xbf, 0x45, 0x52, 0xcf, 0x8e, 0x60,
	0xb7, 0x1a, 0x10, 0x55, 0x67, 0x46, 0xcf, 0x14, 0xb1, 0x6d, 0x64, 0x87, 0xb0, 0x42, 0xd5, 0xff,
	0x68, 0x2a, 0x49, 0x89, 0x9f, 0x06, 0x59, 0x24, 0x66, 0x9d, 0x1f, 0x41, 0xfc, 0x4e, 0x99, 0xd7,
	0xc6, 0xc8, 0xea, 0xfc, 0xa4, 0xfb, 0xae, 0x6d, 0x03, 0x31, 0x04, 0xb4, 0xa1, 0x29, 0x97, 0x15,
	0xf9, 0x3f, 0x0f, 0x1e, 0x5e, 0xc3, 0xee, 0x59, 0xd6, 0x4b, 0x58, 0xf4, 0x84, 0x63, 0xad, 0xf5,
	0xf1, 0xe3, 0x71, 0x2f, 0x37, 0xc2, 0xf3, 0x82, 0x50, 0x38, 0xf0, 0xf0, 0x05, 0x04, 0x05, 0xe1,
	0xbc, 0x64, 0xef, 0xe6, 0xc3, 0xfa, 0xf3, 0xc3, 0xf2, 0x67, 0xb0, 0x5f, 0xa0, 0xea, 0xcb, 0xf3,
	0xc1, 0xd4, 0xfa, 0xf7, 0xe5, 0x95, 0x5c, 0x0f, 0xe4, 0x4f, 0x61, 0x5d, 0x34, 0xdd, 0x99, 0x90,
	0xdd, 0xc5, 0x5d, 0x48, 0x0a, 0x50, 0x2a, 0x73, 0x1f, 0x51, 0xc2, 0xfa, 0x0d, 0x2a, 0x69, 0xd4,
	0x47, 0xb2, 0xc8, 0x13, 0x88, 0x4e, 0xb5, 0x36, 0x64, 0x50, 0xf6, 0x8e, 0x5b, 0x89, 0x2b, 0xc3,
	0x7c, 0xab, 0xfe, 0xf6, 0xad, 0xba, 0x7b, 0x0b, 0xae, 0xee, 0xed, 0x74, 0xe9, 0xfe, 0xc3, 0xab,
	0xff, 0x01, 0x00, 0x00, 0xff, 0xff, 0xaf, 0x37, 0xe1, 0x67, 0x1c, 0x03, 0x00, 0x00,
}
