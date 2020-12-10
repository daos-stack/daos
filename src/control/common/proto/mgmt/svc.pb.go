// Code generated by protoc-gen-go. DO NOT EDIT.
// source: svc.proto

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
	return fileDescriptor_e5747b2e02f0c537, []int{4, 0}
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
	return fileDescriptor_e5747b2e02f0c537, []int{0}
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

type GroupUpdateReq struct {
	MapVersion           uint32                   `protobuf:"varint,1,opt,name=map_version,json=mapVersion,proto3" json:"map_version,omitempty"`
	Servers              []*GroupUpdateReq_Server `protobuf:"bytes,2,rep,name=servers,proto3" json:"servers,omitempty"`
	XXX_NoUnkeyedLiteral struct{}                 `json:"-"`
	XXX_unrecognized     []byte                   `json:"-"`
	XXX_sizecache        int32                    `json:"-"`
}

func (m *GroupUpdateReq) Reset()         { *m = GroupUpdateReq{} }
func (m *GroupUpdateReq) String() string { return proto.CompactTextString(m) }
func (*GroupUpdateReq) ProtoMessage()    {}
func (*GroupUpdateReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_e5747b2e02f0c537, []int{1}
}

func (m *GroupUpdateReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_GroupUpdateReq.Unmarshal(m, b)
}
func (m *GroupUpdateReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_GroupUpdateReq.Marshal(b, m, deterministic)
}
func (m *GroupUpdateReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_GroupUpdateReq.Merge(m, src)
}
func (m *GroupUpdateReq) XXX_Size() int {
	return xxx_messageInfo_GroupUpdateReq.Size(m)
}
func (m *GroupUpdateReq) XXX_DiscardUnknown() {
	xxx_messageInfo_GroupUpdateReq.DiscardUnknown(m)
}

var xxx_messageInfo_GroupUpdateReq proto.InternalMessageInfo

func (m *GroupUpdateReq) GetMapVersion() uint32 {
	if m != nil {
		return m.MapVersion
	}
	return 0
}

func (m *GroupUpdateReq) GetServers() []*GroupUpdateReq_Server {
	if m != nil {
		return m.Servers
	}
	return nil
}

type GroupUpdateReq_Server struct {
	Rank                 uint32   `protobuf:"varint,1,opt,name=rank,proto3" json:"rank,omitempty"`
	Uri                  string   `protobuf:"bytes,2,opt,name=uri,proto3" json:"uri,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *GroupUpdateReq_Server) Reset()         { *m = GroupUpdateReq_Server{} }
func (m *GroupUpdateReq_Server) String() string { return proto.CompactTextString(m) }
func (*GroupUpdateReq_Server) ProtoMessage()    {}
func (*GroupUpdateReq_Server) Descriptor() ([]byte, []int) {
	return fileDescriptor_e5747b2e02f0c537, []int{1, 0}
}

func (m *GroupUpdateReq_Server) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_GroupUpdateReq_Server.Unmarshal(m, b)
}
func (m *GroupUpdateReq_Server) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_GroupUpdateReq_Server.Marshal(b, m, deterministic)
}
func (m *GroupUpdateReq_Server) XXX_Merge(src proto.Message) {
	xxx_messageInfo_GroupUpdateReq_Server.Merge(m, src)
}
func (m *GroupUpdateReq_Server) XXX_Size() int {
	return xxx_messageInfo_GroupUpdateReq_Server.Size(m)
}
func (m *GroupUpdateReq_Server) XXX_DiscardUnknown() {
	xxx_messageInfo_GroupUpdateReq_Server.DiscardUnknown(m)
}

var xxx_messageInfo_GroupUpdateReq_Server proto.InternalMessageInfo

func (m *GroupUpdateReq_Server) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

func (m *GroupUpdateReq_Server) GetUri() string {
	if m != nil {
		return m.Uri
	}
	return ""
}

type GroupUpdateResp struct {
	Status               int32    `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *GroupUpdateResp) Reset()         { *m = GroupUpdateResp{} }
func (m *GroupUpdateResp) String() string { return proto.CompactTextString(m) }
func (*GroupUpdateResp) ProtoMessage()    {}
func (*GroupUpdateResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_e5747b2e02f0c537, []int{2}
}

func (m *GroupUpdateResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_GroupUpdateResp.Unmarshal(m, b)
}
func (m *GroupUpdateResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_GroupUpdateResp.Marshal(b, m, deterministic)
}
func (m *GroupUpdateResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_GroupUpdateResp.Merge(m, src)
}
func (m *GroupUpdateResp) XXX_Size() int {
	return xxx_messageInfo_GroupUpdateResp.Size(m)
}
func (m *GroupUpdateResp) XXX_DiscardUnknown() {
	xxx_messageInfo_GroupUpdateResp.DiscardUnknown(m)
}

var xxx_messageInfo_GroupUpdateResp proto.InternalMessageInfo

func (m *GroupUpdateResp) GetStatus() int32 {
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
	SrvFaultDomain       string   `protobuf:"bytes,6,opt,name=srvFaultDomain,proto3" json:"srvFaultDomain,omitempty"`
	Idx                  uint32   `protobuf:"varint,7,opt,name=idx,proto3" json:"idx,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *JoinReq) Reset()         { *m = JoinReq{} }
func (m *JoinReq) String() string { return proto.CompactTextString(m) }
func (*JoinReq) ProtoMessage()    {}
func (*JoinReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_e5747b2e02f0c537, []int{3}
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

func (m *JoinReq) GetSrvFaultDomain() string {
	if m != nil {
		return m.SrvFaultDomain
	}
	return ""
}

func (m *JoinReq) GetIdx() uint32 {
	if m != nil {
		return m.Idx
	}
	return 0
}

type JoinResp struct {
	Status               int32          `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	Rank                 uint32         `protobuf:"varint,2,opt,name=rank,proto3" json:"rank,omitempty"`
	State                JoinResp_State `protobuf:"varint,3,opt,name=state,proto3,enum=mgmt.JoinResp_State" json:"state,omitempty"`
	FaultDomain          string         `protobuf:"bytes,4,opt,name=faultDomain,proto3" json:"faultDomain,omitempty"`
	LocalJoin            bool           `protobuf:"varint,5,opt,name=localJoin,proto3" json:"localJoin,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *JoinResp) Reset()         { *m = JoinResp{} }
func (m *JoinResp) String() string { return proto.CompactTextString(m) }
func (*JoinResp) ProtoMessage()    {}
func (*JoinResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_e5747b2e02f0c537, []int{4}
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

func (m *JoinResp) GetFaultDomain() string {
	if m != nil {
		return m.FaultDomain
	}
	return ""
}

func (m *JoinResp) GetLocalJoin() bool {
	if m != nil {
		return m.LocalJoin
	}
	return false
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
	return fileDescriptor_e5747b2e02f0c537, []int{5}
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
	return fileDescriptor_e5747b2e02f0c537, []int{6}
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
	AllRanks             bool     `protobuf:"varint,2,opt,name=allRanks,proto3" json:"allRanks,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *GetAttachInfoReq) Reset()         { *m = GetAttachInfoReq{} }
func (m *GetAttachInfoReq) String() string { return proto.CompactTextString(m) }
func (*GetAttachInfoReq) ProtoMessage()    {}
func (*GetAttachInfoReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_e5747b2e02f0c537, []int{7}
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

func (m *GetAttachInfoReq) GetAllRanks() bool {
	if m != nil {
		return m.AllRanks
	}
	return false
}

type GetAttachInfoResp struct {
	Status int32                    `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	Psrs   []*GetAttachInfoResp_Psr `protobuf:"bytes,2,rep,name=psrs,proto3" json:"psrs,omitempty"`
	// These CaRT settings are shared with the
	// libdaos client to aid in CaRT initialization.
	Provider             string   `protobuf:"bytes,3,opt,name=Provider,proto3" json:"Provider,omitempty"`
	Interface            string   `protobuf:"bytes,4,opt,name=Interface,proto3" json:"Interface,omitempty"`
	Domain               string   `protobuf:"bytes,5,opt,name=Domain,proto3" json:"Domain,omitempty"`
	CrtCtxShareAddr      uint32   `protobuf:"varint,6,opt,name=CrtCtxShareAddr,proto3" json:"CrtCtxShareAddr,omitempty"`
	CrtTimeout           uint32   `protobuf:"varint,7,opt,name=CrtTimeout,proto3" json:"CrtTimeout,omitempty"`
	NetDevClass          uint32   `protobuf:"varint,8,opt,name=NetDevClass,proto3" json:"NetDevClass,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *GetAttachInfoResp) Reset()         { *m = GetAttachInfoResp{} }
func (m *GetAttachInfoResp) String() string { return proto.CompactTextString(m) }
func (*GetAttachInfoResp) ProtoMessage()    {}
func (*GetAttachInfoResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_e5747b2e02f0c537, []int{8}
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

func (m *GetAttachInfoResp) GetProvider() string {
	if m != nil {
		return m.Provider
	}
	return ""
}

func (m *GetAttachInfoResp) GetInterface() string {
	if m != nil {
		return m.Interface
	}
	return ""
}

func (m *GetAttachInfoResp) GetDomain() string {
	if m != nil {
		return m.Domain
	}
	return ""
}

func (m *GetAttachInfoResp) GetCrtCtxShareAddr() uint32 {
	if m != nil {
		return m.CrtCtxShareAddr
	}
	return 0
}

func (m *GetAttachInfoResp) GetCrtTimeout() uint32 {
	if m != nil {
		return m.CrtTimeout
	}
	return 0
}

func (m *GetAttachInfoResp) GetNetDevClass() uint32 {
	if m != nil {
		return m.NetDevClass
	}
	return 0
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
	return fileDescriptor_e5747b2e02f0c537, []int{8, 0}
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
	return fileDescriptor_e5747b2e02f0c537, []int{9}
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
	return fileDescriptor_e5747b2e02f0c537, []int{10}
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
	return fileDescriptor_e5747b2e02f0c537, []int{11}
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

type PoolMonitorReq struct {
	PoolUUID             string   `protobuf:"bytes,1,opt,name=poolUUID,proto3" json:"poolUUID,omitempty"`
	PoolHandleUUID       string   `protobuf:"bytes,2,opt,name=poolHandleUUID,proto3" json:"poolHandleUUID,omitempty"`
	Jobid                string   `protobuf:"bytes,3,opt,name=jobid,proto3" json:"jobid,omitempty"`
	Sys                  string   `protobuf:"bytes,4,opt,name=sys,proto3" json:"sys,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *PoolMonitorReq) Reset()         { *m = PoolMonitorReq{} }
func (m *PoolMonitorReq) String() string { return proto.CompactTextString(m) }
func (*PoolMonitorReq) ProtoMessage()    {}
func (*PoolMonitorReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_e5747b2e02f0c537, []int{12}
}

func (m *PoolMonitorReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PoolMonitorReq.Unmarshal(m, b)
}
func (m *PoolMonitorReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PoolMonitorReq.Marshal(b, m, deterministic)
}
func (m *PoolMonitorReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PoolMonitorReq.Merge(m, src)
}
func (m *PoolMonitorReq) XXX_Size() int {
	return xxx_messageInfo_PoolMonitorReq.Size(m)
}
func (m *PoolMonitorReq) XXX_DiscardUnknown() {
	xxx_messageInfo_PoolMonitorReq.DiscardUnknown(m)
}

var xxx_messageInfo_PoolMonitorReq proto.InternalMessageInfo

func (m *PoolMonitorReq) GetPoolUUID() string {
	if m != nil {
		return m.PoolUUID
	}
	return ""
}

func (m *PoolMonitorReq) GetPoolHandleUUID() string {
	if m != nil {
		return m.PoolHandleUUID
	}
	return ""
}

func (m *PoolMonitorReq) GetJobid() string {
	if m != nil {
		return m.Jobid
	}
	return ""
}

func (m *PoolMonitorReq) GetSys() string {
	if m != nil {
		return m.Sys
	}
	return ""
}

func init() {
	proto.RegisterEnum("mgmt.JoinResp_State", JoinResp_State_name, JoinResp_State_value)
	proto.RegisterType((*DaosResp)(nil), "mgmt.DaosResp")
	proto.RegisterType((*GroupUpdateReq)(nil), "mgmt.GroupUpdateReq")
	proto.RegisterType((*GroupUpdateReq_Server)(nil), "mgmt.GroupUpdateReq.Server")
	proto.RegisterType((*GroupUpdateResp)(nil), "mgmt.GroupUpdateResp")
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
	proto.RegisterType((*PoolMonitorReq)(nil), "mgmt.PoolMonitorReq")
}

func init() {
	proto.RegisterFile("svc.proto", fileDescriptor_e5747b2e02f0c537)
}

var fileDescriptor_e5747b2e02f0c537 = []byte{
	// 655 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x8c, 0x54, 0xd1, 0x6e, 0xd3, 0x4a,
	0x10, 0xbd, 0x4e, 0x9c, 0xc4, 0x99, 0xa8, 0x49, 0xee, 0xaa, 0xba, 0xb2, 0x72, 0x11, 0x04, 0x0b,
	0x50, 0x00, 0xc9, 0x48, 0x45, 0xbc, 0x53, 0x25, 0xa2, 0x04, 0x41, 0x09, 0x4e, 0xc3, 0x2b, 0xda,
	0xda, 0xdb, 0xd6, 0xd4, 0xf6, 0x9a, 0xdd, 0x75, 0x68, 0x25, 0x3e, 0x84, 0x3f, 0xe0, 0x27, 0x78,
	0xe7, 0xb7, 0xd0, 0xec, 0x3a, 0x6e, 0x92, 0xd2, 0x8a, 0xb7, 0x99, 0xb3, 0x67, 0xcf, 0xcc, 0xce,
	0x1c, 0x1b, 0xda, 0x72, 0x19, 0xfa, 0xb9, 0xe0, 0x8a, 0x13, 0x3b, 0x3d, 0x4d, 0x95, 0xe7, 0x81,
	0x33, 0xa1, 0x5c, 0x06, 0x4c, 0xe6, 0xe4, 0x3f, 0x68, 0x4a, 0x45, 0x55, 0x21, 0x5d, 0x6b, 0x68,
	0x8d, 0x1a, 0x41, 0x99, 0x79, 0xdf, 0x2d, 0xe8, 0x1e, 0x08, 0x5e, 0xe4, 0x8b, 0x3c, 0xa2, 0x8a,
	0x05, 0xec, 0x0b, 0xb9, 0x07, 0x9d, 0x94, 0xe6, 0x9f, 0x96, 0x4c, 0xc8, 0x98, 0x67, 0x9a, 0xbf,
	0x13, 0x40, 0x4a, 0xf3, 0x8f, 0x06, 0x21, 0x2f, 0xa0, 0x25, 0x99, 0xc0, 0x73, 0xb7, 0x36, 0xac,
	0x8f, 0x3a, 0x7b, 0xff, 0xfb, 0x58, 0xcf, 0xdf, 0xd4, 0xf1, 0xe7, 0x9a, 0x13, 0xac, 0xb8, 0x03,
	0x1f, 0x9a, 0x06, 0x22, 0x04, 0x6c, 0x41, 0xb3, 0xf3, 0x52, 0x5a, 0xc7, 0xa4, 0x0f, 0xf5, 0x42,
	0xc4, 0x6e, 0x6d, 0x68, 0x8d, 0xda, 0x01, 0x86, 0xde, 0x63, 0xe8, 0x6d, 0x28, 0xde, 0xf2, 0x8a,
	0x1f, 0x16, 0xb4, 0xde, 0xf0, 0x38, 0xc3, 0xf6, 0x09, 0xd8, 0x45, 0x11, 0x47, 0x9a, 0xd1, 0x0e,
	0x74, 0x5c, 0x15, 0xac, 0x5d, 0x2f, 0x58, 0xaf, 0x0a, 0x92, 0x5d, 0x68, 0x64, 0xa1, 0xba, 0x90,
	0xae, 0xad, 0x69, 0x26, 0xc1, 0xbb, 0x34, 0x8a, 0x84, 0xdb, 0x30, 0x7a, 0x18, 0x93, 0x47, 0xd0,
	0x95, 0x62, 0xf9, 0x8a, 0x16, 0x89, 0x9a, 0xf0, 0x94, 0xc6, 0x99, 0xdb, 0xd4, 0xa7, 0x5b, 0x28,
	0xd6, 0x88, 0xa3, 0x0b, 0xb7, 0xa5, 0xf5, 0x30, 0xf4, 0x7e, 0x5a, 0xe0, 0x98, 0x4e, 0x6f, 0x7e,
	0xce, 0x1f, 0xdb, 0x7d, 0x02, 0x0d, 0x3c, 0x65, 0xba, 0xe1, 0xee, 0xde, 0xae, 0x19, 0xf9, 0x4a,
	0xca, 0x9f, 0xe3, 0x59, 0x60, 0x28, 0x64, 0x08, 0x9d, 0x93, 0xb5, 0xde, 0x6c, 0xdd, 0xdb, 0x3a,
	0x44, 0xee, 0x40, 0x3b, 0xe1, 0x21, 0x4d, 0xf0, 0xbe, 0x7e, 0x99, 0x13, 0x5c, 0x01, 0x9e, 0x0b,
	0x0d, 0xad, 0x47, 0x9a, 0x50, 0x9b, 0x1e, 0xf6, 0xff, 0x21, 0x2d, 0xa8, 0xbf, 0x5f, 0x1c, 0xf5,
	0x2d, 0x6f, 0x04, 0xdd, 0xb7, 0x8c, 0x46, 0x4c, 0x7c, 0x28, 0x98, 0xb8, 0xc4, 0x71, 0xe3, 0x1b,
	0x2e, 0xa5, 0x62, 0x69, 0x39, 0xf0, 0x32, 0xf3, 0xe6, 0xd0, 0xdb, 0x60, 0xca, 0x9c, 0x3c, 0x80,
	0x9d, 0xb0, 0x10, 0x82, 0x65, 0xca, 0x9c, 0x94, 0x37, 0x36, 0x41, 0x32, 0x00, 0x47, 0xb0, 0x3c,
	0x89, 0x43, 0x6a, 0xec, 0xd5, 0x0e, 0xaa, 0xdc, 0x7b, 0x09, 0xfd, 0x03, 0xa6, 0xf6, 0x95, 0xa2,
	0xe1, 0xd9, 0x34, 0x3b, 0xe1, 0xd8, 0x40, 0x1f, 0xea, 0xf2, 0x52, 0x96, 0x5a, 0x18, 0xa2, 0x02,
	0x4d, 0x92, 0x80, 0x66, 0xe7, 0x52, 0x8f, 0xd0, 0x09, 0xaa, 0xdc, 0xfb, 0x55, 0x83, 0x7f, 0xb7,
	0x24, 0x6e, 0x59, 0xc4, 0x33, 0xb0, 0x73, 0x79, 0xcd, 0xe6, 0xdb, 0xd7, 0xfd, 0x99, 0x14, 0x81,
	0x26, 0x62, 0xe9, 0x99, 0xe0, 0xcb, 0x18, 0x5f, 0x67, 0x9c, 0x55, 0xe5, 0x38, 0xf3, 0x69, 0xa6,
	0x98, 0x38, 0xa1, 0x21, 0x2b, 0x77, 0x72, 0x05, 0x60, 0x0b, 0xe5, 0xba, 0x8c, 0xd1, 0xca, 0x8c,
	0x8c, 0xa0, 0x37, 0x16, 0x6a, 0xac, 0x2e, 0xe6, 0x67, 0x54, 0xb0, 0x7d, 0x74, 0x62, 0x53, 0xdb,
	0x62, 0x1b, 0x26, 0x77, 0x01, 0xc6, 0x42, 0x1d, 0xc5, 0x29, 0xe3, 0x85, 0x2a, 0x3d, 0xb7, 0x86,
	0xa0, 0x2b, 0x0e, 0x99, 0x9a, 0xb0, 0xe5, 0x38, 0xa1, 0x52, 0xba, 0x8e, 0x26, 0xac, 0x43, 0x83,
	0xa7, 0x50, 0x9f, 0xc9, 0xbf, 0xfd, 0x3c, 0x1f, 0x42, 0x6f, 0x26, 0x58, 0x3e, 0x3f, 0x2b, 0x54,
	0xc4, 0xbf, 0xae, 0x3e, 0xbd, 0xed, 0x8b, 0xde, 0x7d, 0xe8, 0xcc, 0xe2, 0xec, 0x14, 0xa7, 0x7f,
	0x13, 0x65, 0x08, 0x30, 0x67, 0xea, 0x36, 0xc6, 0x37, 0xe8, 0xce, 0x38, 0x4f, 0xde, 0xf1, 0x2c,
	0x56, 0x5c, 0x20, 0x6b, 0x00, 0x4e, 0xce, 0x79, 0xb2, 0x58, 0x4c, 0x27, 0xe5, 0xea, 0xab, 0x1c,
	0xbf, 0x4e, 0x8c, 0x5f, 0xd3, 0x2c, 0x4a, 0x98, 0x66, 0x98, 0xb6, 0xb7, 0x50, 0xfc, 0xde, 0x3f,
	0xf3, 0xe3, 0x38, 0x2a, 0x37, 0x65, 0x92, 0x95, 0x9f, 0xec, 0xca, 0x4f, 0xc7, 0x4d, 0xfd, 0x53,
	0x7d, 0xfe, 0x3b, 0x00, 0x00, 0xff, 0xff, 0xc4, 0x03, 0x5a, 0xf2, 0x61, 0x05, 0x00, 0x00,
}
