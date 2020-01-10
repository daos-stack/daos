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

type KillRankReq struct {
	Force                bool     `protobuf:"varint,1,opt,name=force,proto3" json:"force,omitempty"`
	Rank                 uint32   `protobuf:"varint,2,opt,name=rank,proto3" json:"rank,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *KillRankReq) Reset()         { *m = KillRankReq{} }
func (m *KillRankReq) String() string { return proto.CompactTextString(m) }
func (*KillRankReq) ProtoMessage()    {}
func (*KillRankReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{8}
}

func (m *KillRankReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_KillRankReq.Unmarshal(m, b)
}
func (m *KillRankReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_KillRankReq.Marshal(b, m, deterministic)
}
func (m *KillRankReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_KillRankReq.Merge(m, src)
}
func (m *KillRankReq) XXX_Size() int {
	return xxx_messageInfo_KillRankReq.Size(m)
}
func (m *KillRankReq) XXX_DiscardUnknown() {
	xxx_messageInfo_KillRankReq.DiscardUnknown(m)
}

var xxx_messageInfo_KillRankReq proto.InternalMessageInfo

func (m *KillRankReq) GetForce() bool {
	if m != nil {
		return m.Force
	}
	return false
}

func (m *KillRankReq) GetRank() uint32 {
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
	return fileDescriptor_2bbe8325d22c1a26, []int{9}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{10}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{11}
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
	proto.RegisterType((*KillRankReq)(nil), "mgmt.KillRankReq")
	proto.RegisterType((*PingRankReq)(nil), "mgmt.PingRankReq")
	proto.RegisterType((*SetRankReq)(nil), "mgmt.SetRankReq")
	proto.RegisterType((*CreateMsReq)(nil), "mgmt.CreateMsReq")
}

func init() { proto.RegisterFile("srv.proto", fileDescriptor_2bbe8325d22c1a26) }

var fileDescriptor_2bbe8325d22c1a26 = []byte{
	// 440 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x7c, 0x93, 0x6d, 0x6f, 0xd3, 0x30,
	0x10, 0xc7, 0x49, 0xd2, 0x74, 0xed, 0x55, 0xdb, 0x82, 0x55, 0xa1, 0x68, 0xf0, 0xa2, 0x58, 0x43,
	0xaa, 0x40, 0x0a, 0xd2, 0x78, 0xc1, 0x6b, 0x04, 0x12, 0x1a, 0x8f, 0xc5, 0x81, 0x0f, 0xe0, 0x25,
	0xde, 0x16, 0xad, 0x8d, 0xc3, 0x9d, 0x03, 0x54, 0xe2, 0x0b, 0xf0, 0xad, 0x91, 0xed, 0x34, 0x53,
	0xb7, 0x6e, 0xef, 0xee, 0x6f, 0xff, 0xee, 0xc1, 0x77, 0x67, 0x18, 0x13, 0xfe, 0xca, 0x1a, 0xd4,
	0x46, 0xb3, 0xc1, 0xea, 0x62, 0x65, 0x38, 0x87, 0xd1, 0x3b, 0xa9, 0x49, 0x28, 0x6a, 0xd8, 0x23,
	0x18, 0x92, 0x91, 0xa6, 0xa5, 0x34, 0x98, 0x05, 0xf3, 0x58, 0x74, 0x8a, 0xaf, 0x60, 0xef, 0x83,
	0xae, 0x6a, 0xa1, 0x7e, 0x32, 0x06, 0x83, 0xb6, 0xad, 0x4a, 0x07, 0x8c, 0x85, 0xb3, 0xed, 0x19,
	0xca, 0xfa, 0x2a, 0x0d, 0x67, 0xc1, 0x7c, 0x5f, 0x38, 0x9b, 0x25, 0x10, 0xb5, 0x58, 0xa5, 0x91,
	0xc3, 0xac, 0xc9, 0xa6, 0x10, 0xd7, 0x85, 0xf9, 0x43, 0xe9, 0xc0, 0x61, 0x5e, 0x58, 0x5f, 0x59,
	0x96, 0x98, 0xc6, 0x3e, 0x9e, 0xb5, 0xf9, 0x5f, 0x18, 0xf9, 0x74, 0x77, 0x97, 0xb4, 0x33, 0xe7,
	0x73, 0x88, 0xed, 0xad, 0x72, 0x59, 0x0f, 0x4e, 0xa6, 0x99, 0x7d, 0x60, 0xb6, 0x09, 0x95, 0xe5,
	0xf6, 0x4e, 0x78, 0x84, 0xa7, 0x10, 0x3b, 0xcd, 0x86, 0x10, 0x9e, 0x7e, 0x49, 0x1e, 0xb0, 0x3d,
	0x88, 0xbe, 0xfe, 0xf8, 0x9e, 0x04, 0x7c, 0x0e, 0x07, 0x9f, 0x94, 0x2c, 0x15, 0x7e, 0x6b, 0x15,
	0xae, 0xed, 0x9b, 0x6d, 0x0d, 0x6b, 0x32, 0x6a, 0xd5, 0xbd, 0xba, 0x53, 0x3c, 0x87, 0xc3, 0x2d,
	0x92, 0x1a, 0x76, 0x0c, 0xfb, 0x45, 0x8b, 0xa8, 0x6a, 0xe3, 0x6f, 0x3a, 0x8f, 0xed, 0x43, 0x76,
	0x04, 0x23, 0x54, 0xcd, 0xb2, 0x2a, 0x24, 0xa5, 0xe1, 0x2c, 0x9a, 0x8f, 0x45, 0xaf, 0xf9, 0x31,
	0x24, 0xef, 0x95, 0x79, 0x63, 0x8c, 0x2c, 0x2e, 0x4f, 0xeb, 0x73, 0x6d, 0x0b, 0x48, 0x20, 0xa2,
	0x35, 0x75, 0xb1, 0xac, 0xc9, 0xff, 0x05, 0xf0, 0xf0, 0x06, 0x76, 0x4f, 0xb3, 0x5e, 0xc2, 0xa0,
	0x21, 0xf4, 0xb9, 0x26, 0x27, 0x8f, 0x7d, 0x5f, 0x6e, 0xb9, 0x67, 0x0b, 0x42, 0xe1, 0xc0, 0xa3,
	0x17, 0x10, 0x2d, 0x08, 0xfb, 0x26, 0x07, 0xb7, 0x07, 0x1b, 0xf6, 0x83, 0xe5, 0xcf, 0xe0, 0x70,
	0x81, 0xaa, 0xc9, 0x2f, 0x5b, 0x53, 0xea, 0xdf, 0x9b, 0x2d, 0xb9, 0xe9, 0xc8, 0x5f, 0xc3, 0xe4,
	0x63, 0xb5, 0x5c, 0x0a, 0x59, 0x5f, 0x59, 0x64, 0x0a, 0xf1, 0xb9, 0xc6, 0x42, 0x39, 0x66, 0x24,
	0xbc, 0xd8, 0x35, 0x56, 0xfe, 0x14, 0x26, 0x8b, 0xaa, 0xbe, 0xd8, 0x38, 0xee, 0x8a, 0x3d, 0x03,
	0xc8, 0x95, 0xb9, 0x8f, 0xc8, 0x61, 0xf2, 0x16, 0x95, 0x34, 0xea, 0x33, 0x59, 0xe4, 0x09, 0x8c,
	0xcf, 0xb4, 0x36, 0x64, 0x50, 0x36, 0x5d, 0x05, 0xd7, 0x07, 0xfd, 0x92, 0x87, 0xdb, 0x4b, 0xee,
	0x16, 0x35, 0xba, 0x5e, 0xd4, 0xb3, 0xa1, 0xfb, 0x48, 0xaf, 0xfe, 0x07, 0x00, 0x00, 0xff, 0xff,
	0xf6, 0x50, 0x56, 0x9f, 0x55, 0x03, 0x00, 0x00,
}
