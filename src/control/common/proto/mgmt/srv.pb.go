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
	return fileDescriptor_2bbe8325d22c1a26, []int{4, 0}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{1}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{1, 0}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{2}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{3}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{4}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{5}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{6}
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
	Jobid                string   `protobuf:"bytes,3,opt,name=jobid,proto3" json:"jobid,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *GetAttachInfoReq) Reset()         { *m = GetAttachInfoReq{} }
func (m *GetAttachInfoReq) String() string { return proto.CompactTextString(m) }
func (*GetAttachInfoReq) ProtoMessage()    {}
func (*GetAttachInfoReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_2bbe8325d22c1a26, []int{7}
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

func (m *GetAttachInfoReq) GetJobid() string {
	if m != nil {
		return m.Jobid
	}
	return ""
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
	return fileDescriptor_2bbe8325d22c1a26, []int{8}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{8, 0}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{9}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{10}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{11}
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
	return fileDescriptor_2bbe8325d22c1a26, []int{12}
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
	proto.RegisterType((*CreateMsReq)(nil), "mgmt.CreateMsReq")
}

func init() {
	proto.RegisterFile("srv.proto", fileDescriptor_2bbe8325d22c1a26)
}

var fileDescriptor_2bbe8325d22c1a26 = []byte{
	// 645 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x8c, 0x54, 0xdd, 0x6e, 0xd3, 0x4c,
	0x10, 0xfd, 0x1c, 0xe7, 0xc7, 0x99, 0xa8, 0x49, 0xbe, 0x55, 0x85, 0xac, 0x52, 0x41, 0xb0, 0x00,
	0x05, 0x90, 0x8c, 0x54, 0xc4, 0x03, 0x54, 0xa9, 0xa8, 0x8a, 0xa0, 0x84, 0x75, 0xdb, 0x5b, 0xb4,
	0x8d, 0xb7, 0xad, 0xa9, 0xed, 0x35, 0xbb, 0xeb, 0xd0, 0xbe, 0x09, 0x6f, 0xc0, 0x4b, 0x70, 0xcf,
	0x6b, 0xa1, 0xd9, 0x75, 0x9d, 0x9f, 0xd2, 0x8a, 0xbb, 0x99, 0xb3, 0x67, 0xce, 0xcc, 0xec, 0xcc,
	0x2e, 0x74, 0x95, 0x9c, 0x87, 0x85, 0x14, 0x5a, 0x90, 0x66, 0x76, 0x9e, 0xe9, 0x20, 0x00, 0x6f,
	0x8f, 0x09, 0x45, 0xb9, 0x2a, 0xc8, 0x03, 0x68, 0x2b, 0xcd, 0x74, 0xa9, 0x7c, 0x67, 0xe4, 0x8c,
	0x5b, 0xb4, 0xf2, 0x82, 0x1f, 0x0e, 0xf4, 0xf7, 0xa5, 0x28, 0x8b, 0xe3, 0x22, 0x66, 0x9a, 0x53,
	0xfe, 0x8d, 0x3c, 0x86, 0x5e, 0xc6, 0x8a, 0x2f, 0x73, 0x2e, 0x55, 0x22, 0x72, 0xc3, 0xdf, 0xa0,
	0x90, 0xb1, 0xe2, 0xc4, 0x22, 0xe4, 0x2d, 0x74, 0x14, 0x97, 0x78, 0xee, 0x37, 0x46, 0xee, 0xb8,
	0xb7, 0xf3, 0x30, 0xc4, 0x7c, 0xe1, 0xaa, 0x4e, 0x18, 0x19, 0x0e, 0xbd, 0xe1, 0x6e, 0x85, 0xd0,
	0xb6, 0x10, 0x21, 0xd0, 0x94, 0x2c, 0xbf, 0xac, 0xa4, 0x8d, 0x4d, 0x86, 0xe0, 0x96, 0x32, 0xf1,
	0x1b, 0x23, 0x67, 0xdc, 0xa5, 0x68, 0x06, 0x2f, 0x60, 0xb0, 0xa2, 0x78, 0x4f, 0x17, 0x3f, 0x1d,
	0xe8, 0xbc, 0x17, 0x49, 0x8e, 0xe5, 0x13, 0x68, 0x96, 0x65, 0x12, 0x1b, 0x46, 0x97, 0x1a, 0xbb,
	0x4e, 0xd8, 0xb8, 0x9d, 0xd0, 0xad, 0x13, 0x92, 0x4d, 0x68, 0xe5, 0x33, 0x7d, 0xa5, 0xfc, 0xa6,
	0xa1, 0x59, 0x07, 0x63, 0x59, 0x1c, 0x4b, 0xbf, 0x65, 0xf5, 0xd0, 0x26, 0xcf, 0xa1, 0xaf, 0xe4,
	0xfc, 0x1d, 0x2b, 0x53, 0xbd, 0x27, 0x32, 0x96, 0xe4, 0x7e, 0xdb, 0x9c, 0xae, 0xa1, 0x98, 0x23,
	0x89, 0xaf, 0xfc, 0x8e, 0xd1, 0x43, 0x33, 0xf8, 0xe5, 0x80, 0x67, 0x2b, 0xbd, 0xbb, 0x9d, 0xbf,
	0x96, 0xfb, 0x12, 0x5a, 0x78, 0xca, 0x4d, 0xc1, 0xfd, 0x9d, 0x4d, 0x7b, 0xe5, 0x37, 0x52, 0x61,
	0x84, 0x67, 0xd4, 0x52, 0xc8, 0x08, 0x7a, 0x67, 0x4b, 0xb5, 0x35, 0x4d, 0x6d, 0xcb, 0x10, 0xd9,
	0x86, 0x6e, 0x2a, 0x66, 0x2c, 0xc5, 0x78, 0xd3, 0x99, 0x47, 0x17, 0x40, 0xe0, 0x43, 0xcb, 0xe8,
	0x91, 0x36, 0x34, 0x0e, 0x0e, 0x87, 0xff, 0x91, 0x0e, 0xb8, 0x9f, 0x8e, 0x8f, 0x86, 0x4e, 0x30,
	0x86, 0xfe, 0x07, 0xce, 0x62, 0x2e, 0x3f, 0x97, 0x5c, 0x5e, 0xe3, 0x75, 0x63, 0x0f, 0xd7, 0x4a,
	0xf3, 0xac, 0xba, 0xf0, 0xca, 0x0b, 0x22, 0x18, 0xac, 0x30, 0x55, 0x41, 0x9e, 0xc2, 0xc6, 0xac,
	0x94, 0x92, 0xe7, 0xda, 0x9e, 0x54, 0x11, 0xab, 0x20, 0xd9, 0x02, 0x4f, 0xf2, 0x22, 0x4d, 0x66,
	0xcc, 0xae, 0x57, 0x97, 0xd6, 0x7e, 0x70, 0x02, 0xc3, 0x7d, 0xae, 0x77, 0xb5, 0x66, 0xb3, 0x8b,
	0x83, 0xfc, 0x4c, 0x60, 0x01, 0x43, 0x70, 0xd5, 0xb5, 0xaa, 0xb4, 0xd0, 0x44, 0x05, 0x96, 0xa6,
	0x94, 0xe5, 0x97, 0xca, 0x5c, 0xa1, 0x47, 0x6b, 0x1f, 0x67, 0xfc, 0x55, 0x9c, 0x26, 0x71, 0x35,
	0x77, 0xeb, 0x04, 0xbf, 0x1b, 0xf0, 0xff, 0x9a, 0xf0, 0x3d, 0xe3, 0x79, 0x0d, 0xcd, 0x42, 0xdd,
	0x5a, 0xfe, 0xf5, 0xf0, 0x70, 0xaa, 0x24, 0x35, 0x44, 0x2c, 0x68, 0x2a, 0xc5, 0x3c, 0xc1, 0x9e,
	0x6d, 0xde, 0xda, 0xc7, 0x49, 0x1c, 0xe4, 0x9a, 0xcb, 0x33, 0x36, 0xe3, 0xd5, 0xa4, 0x16, 0x00,
	0x96, 0x50, 0x0d, 0xd1, 0xae, 0x5f, 0xe5, 0x91, 0x31, 0x0c, 0x26, 0x52, 0x4f, 0xf4, 0x55, 0x74,
	0xc1, 0x24, 0xdf, 0xc5, 0xfd, 0x6c, 0x9b, 0x65, 0x59, 0x87, 0xc9, 0x23, 0x80, 0x89, 0xd4, 0x47,
	0x49, 0xc6, 0x45, 0xa9, 0xab, 0x4d, 0x5c, 0x42, 0x70, 0x57, 0x0e, 0xb9, 0xde, 0xe3, 0xf3, 0x49,
	0xca, 0x94, 0xf2, 0x3d, 0x43, 0x58, 0x86, 0xb6, 0x5e, 0x81, 0x3b, 0x55, 0xff, 0xfa, 0x68, 0x9f,
	0xc1, 0x60, 0x2a, 0x79, 0x11, 0x5d, 0x94, 0x3a, 0x16, 0xdf, 0x6f, 0x1e, 0xe4, 0x7a, 0x60, 0xf0,
	0x04, 0x7a, 0xd3, 0x24, 0x3f, 0xc7, 0x99, 0xdc, 0x45, 0x19, 0x01, 0x44, 0x5c, 0xdf, 0xc7, 0x88,
	0xa0, 0x37, 0x91, 0x9c, 0x69, 0xfe, 0x51, 0x21, 0x65, 0x1b, 0xba, 0xa7, 0x42, 0x68, 0xa5, 0x25,
	0x2b, 0x0c, 0xcf, 0xa3, 0x0b, 0xa0, 0xfe, 0x16, 0x1a, 0xab, 0xdf, 0x82, 0x79, 0xda, 0xee, 0xe2,
	0x69, 0x9f, 0xb6, 0xcd, 0x0f, 0xfa, 0xe6, 0x4f, 0x00, 0x00, 0x00, 0xff, 0xff, 0x78, 0x68, 0x69,
	0xbf, 0x4e, 0x05, 0x00, 0x00,
}
