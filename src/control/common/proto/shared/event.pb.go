// Code generated by protoc-gen-go. DO NOT EDIT.
// source: shared/event.proto

package shared

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

// RASEvent describes a RAS event in the DAOS system.
type RASEvent struct {
	Id        uint32 `protobuf:"varint,1,opt,name=id,proto3" json:"id,omitempty"`
	Msg       string `protobuf:"bytes,2,opt,name=msg,proto3" json:"msg,omitempty"`
	Timestamp string `protobuf:"bytes,3,opt,name=timestamp,proto3" json:"timestamp,omitempty"`
	Type      uint32 `protobuf:"varint,4,opt,name=type,proto3" json:"type,omitempty"`
	Severity  uint32 `protobuf:"varint,5,opt,name=severity,proto3" json:"severity,omitempty"`
	Hostname  string `protobuf:"bytes,6,opt,name=hostname,proto3" json:"hostname,omitempty"`
	Rank      uint32 `protobuf:"varint,7,opt,name=rank,proto3" json:"rank,omitempty"`
	HwId      string `protobuf:"bytes,8,opt,name=hw_id,json=hwId,proto3" json:"hw_id,omitempty"`
	ProcId    string `protobuf:"bytes,9,opt,name=proc_id,json=procId,proto3" json:"proc_id,omitempty"`
	ThreadId  string `protobuf:"bytes,10,opt,name=thread_id,json=threadId,proto3" json:"thread_id,omitempty"`
	JobId     string `protobuf:"bytes,11,opt,name=job_id,json=jobId,proto3" json:"job_id,omitempty"`
	PoolUuid  string `protobuf:"bytes,12,opt,name=pool_uuid,json=poolUuid,proto3" json:"pool_uuid,omitempty"`
	ContUuid  string `protobuf:"bytes,13,opt,name=cont_uuid,json=contUuid,proto3" json:"cont_uuid,omitempty"`
	ObjId     string `protobuf:"bytes,14,opt,name=obj_id,json=objId,proto3" json:"obj_id,omitempty"`
	CtlOp     string `protobuf:"bytes,15,opt,name=ctl_op,json=ctlOp,proto3" json:"ctl_op,omitempty"`
	// Types that are valid to be assigned to ExtendedInfo:
	//	*RASEvent_StrInfo
	//	*RASEvent_RankStateInfo
	//	*RASEvent_PoolSvcInfo
	ExtendedInfo         isRASEvent_ExtendedInfo `protobuf_oneof:"extended_info"`
	XXX_NoUnkeyedLiteral struct{}                `json:"-"`
	XXX_unrecognized     []byte                  `json:"-"`
	XXX_sizecache        int32                   `json:"-"`
}

func (m *RASEvent) Reset()         { *m = RASEvent{} }
func (m *RASEvent) String() string { return proto.CompactTextString(m) }
func (*RASEvent) ProtoMessage()    {}
func (*RASEvent) Descriptor() ([]byte, []int) {
	return fileDescriptor_462d23cf02562d79, []int{0}
}

func (m *RASEvent) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_RASEvent.Unmarshal(m, b)
}
func (m *RASEvent) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_RASEvent.Marshal(b, m, deterministic)
}
func (m *RASEvent) XXX_Merge(src proto.Message) {
	xxx_messageInfo_RASEvent.Merge(m, src)
}
func (m *RASEvent) XXX_Size() int {
	return xxx_messageInfo_RASEvent.Size(m)
}
func (m *RASEvent) XXX_DiscardUnknown() {
	xxx_messageInfo_RASEvent.DiscardUnknown(m)
}

var xxx_messageInfo_RASEvent proto.InternalMessageInfo

func (m *RASEvent) GetId() uint32 {
	if m != nil {
		return m.Id
	}
	return 0
}

func (m *RASEvent) GetMsg() string {
	if m != nil {
		return m.Msg
	}
	return ""
}

func (m *RASEvent) GetTimestamp() string {
	if m != nil {
		return m.Timestamp
	}
	return ""
}

func (m *RASEvent) GetType() uint32 {
	if m != nil {
		return m.Type
	}
	return 0
}

func (m *RASEvent) GetSeverity() uint32 {
	if m != nil {
		return m.Severity
	}
	return 0
}

func (m *RASEvent) GetHostname() string {
	if m != nil {
		return m.Hostname
	}
	return ""
}

func (m *RASEvent) GetRank() uint32 {
	if m != nil {
		return m.Rank
	}
	return 0
}

func (m *RASEvent) GetHwId() string {
	if m != nil {
		return m.HwId
	}
	return ""
}

func (m *RASEvent) GetProcId() string {
	if m != nil {
		return m.ProcId
	}
	return ""
}

func (m *RASEvent) GetThreadId() string {
	if m != nil {
		return m.ThreadId
	}
	return ""
}

func (m *RASEvent) GetJobId() string {
	if m != nil {
		return m.JobId
	}
	return ""
}

func (m *RASEvent) GetPoolUuid() string {
	if m != nil {
		return m.PoolUuid
	}
	return ""
}

func (m *RASEvent) GetContUuid() string {
	if m != nil {
		return m.ContUuid
	}
	return ""
}

func (m *RASEvent) GetObjId() string {
	if m != nil {
		return m.ObjId
	}
	return ""
}

func (m *RASEvent) GetCtlOp() string {
	if m != nil {
		return m.CtlOp
	}
	return ""
}

type isRASEvent_ExtendedInfo interface {
	isRASEvent_ExtendedInfo()
}

type RASEvent_StrInfo struct {
	StrInfo string `protobuf:"bytes,16,opt,name=str_info,json=strInfo,proto3,oneof"`
}

type RASEvent_RankStateInfo struct {
	RankStateInfo *RASEvent_RankStateEventInfo `protobuf:"bytes,17,opt,name=rank_state_info,json=rankStateInfo,proto3,oneof"`
}

type RASEvent_PoolSvcInfo struct {
	PoolSvcInfo *RASEvent_PoolSvcEventInfo `protobuf:"bytes,18,opt,name=pool_svc_info,json=poolSvcInfo,proto3,oneof"`
}

func (*RASEvent_StrInfo) isRASEvent_ExtendedInfo() {}

func (*RASEvent_RankStateInfo) isRASEvent_ExtendedInfo() {}

func (*RASEvent_PoolSvcInfo) isRASEvent_ExtendedInfo() {}

func (m *RASEvent) GetExtendedInfo() isRASEvent_ExtendedInfo {
	if m != nil {
		return m.ExtendedInfo
	}
	return nil
}

func (m *RASEvent) GetStrInfo() string {
	if x, ok := m.GetExtendedInfo().(*RASEvent_StrInfo); ok {
		return x.StrInfo
	}
	return ""
}

func (m *RASEvent) GetRankStateInfo() *RASEvent_RankStateEventInfo {
	if x, ok := m.GetExtendedInfo().(*RASEvent_RankStateInfo); ok {
		return x.RankStateInfo
	}
	return nil
}

func (m *RASEvent) GetPoolSvcInfo() *RASEvent_PoolSvcEventInfo {
	if x, ok := m.GetExtendedInfo().(*RASEvent_PoolSvcInfo); ok {
		return x.PoolSvcInfo
	}
	return nil
}

// XXX_OneofWrappers is for the internal use of the proto package.
func (*RASEvent) XXX_OneofWrappers() []interface{} {
	return []interface{}{
		(*RASEvent_StrInfo)(nil),
		(*RASEvent_RankStateInfo)(nil),
		(*RASEvent_PoolSvcInfo)(nil),
	}
}

// RankStateEventInfo defines extended fields for rank state change events.
type RASEvent_RankStateEventInfo struct {
	Instance             uint32   `protobuf:"varint,1,opt,name=instance,proto3" json:"instance,omitempty"`
	Errored              bool     `protobuf:"varint,2,opt,name=errored,proto3" json:"errored,omitempty"`
	Error                string   `protobuf:"bytes,3,opt,name=error,proto3" json:"error,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *RASEvent_RankStateEventInfo) Reset()         { *m = RASEvent_RankStateEventInfo{} }
func (m *RASEvent_RankStateEventInfo) String() string { return proto.CompactTextString(m) }
func (*RASEvent_RankStateEventInfo) ProtoMessage()    {}
func (*RASEvent_RankStateEventInfo) Descriptor() ([]byte, []int) {
	return fileDescriptor_462d23cf02562d79, []int{0, 0}
}

func (m *RASEvent_RankStateEventInfo) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_RASEvent_RankStateEventInfo.Unmarshal(m, b)
}
func (m *RASEvent_RankStateEventInfo) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_RASEvent_RankStateEventInfo.Marshal(b, m, deterministic)
}
func (m *RASEvent_RankStateEventInfo) XXX_Merge(src proto.Message) {
	xxx_messageInfo_RASEvent_RankStateEventInfo.Merge(m, src)
}
func (m *RASEvent_RankStateEventInfo) XXX_Size() int {
	return xxx_messageInfo_RASEvent_RankStateEventInfo.Size(m)
}
func (m *RASEvent_RankStateEventInfo) XXX_DiscardUnknown() {
	xxx_messageInfo_RASEvent_RankStateEventInfo.DiscardUnknown(m)
}

var xxx_messageInfo_RASEvent_RankStateEventInfo proto.InternalMessageInfo

func (m *RASEvent_RankStateEventInfo) GetInstance() uint32 {
	if m != nil {
		return m.Instance
	}
	return 0
}

func (m *RASEvent_RankStateEventInfo) GetErrored() bool {
	if m != nil {
		return m.Errored
	}
	return false
}

func (m *RASEvent_RankStateEventInfo) GetError() string {
	if m != nil {
		return m.Error
	}
	return ""
}

// PoolSvcEventInfo defines extended fields for pool service change events.
type RASEvent_PoolSvcEventInfo struct {
	SvcReps              []uint32 `protobuf:"varint,1,rep,packed,name=svc_reps,json=svcReps,proto3" json:"svc_reps,omitempty"`
	Version              uint64   `protobuf:"varint,2,opt,name=version,proto3" json:"version,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *RASEvent_PoolSvcEventInfo) Reset()         { *m = RASEvent_PoolSvcEventInfo{} }
func (m *RASEvent_PoolSvcEventInfo) String() string { return proto.CompactTextString(m) }
func (*RASEvent_PoolSvcEventInfo) ProtoMessage()    {}
func (*RASEvent_PoolSvcEventInfo) Descriptor() ([]byte, []int) {
	return fileDescriptor_462d23cf02562d79, []int{0, 1}
}

func (m *RASEvent_PoolSvcEventInfo) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_RASEvent_PoolSvcEventInfo.Unmarshal(m, b)
}
func (m *RASEvent_PoolSvcEventInfo) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_RASEvent_PoolSvcEventInfo.Marshal(b, m, deterministic)
}
func (m *RASEvent_PoolSvcEventInfo) XXX_Merge(src proto.Message) {
	xxx_messageInfo_RASEvent_PoolSvcEventInfo.Merge(m, src)
}
func (m *RASEvent_PoolSvcEventInfo) XXX_Size() int {
	return xxx_messageInfo_RASEvent_PoolSvcEventInfo.Size(m)
}
func (m *RASEvent_PoolSvcEventInfo) XXX_DiscardUnknown() {
	xxx_messageInfo_RASEvent_PoolSvcEventInfo.DiscardUnknown(m)
}

var xxx_messageInfo_RASEvent_PoolSvcEventInfo proto.InternalMessageInfo

func (m *RASEvent_PoolSvcEventInfo) GetSvcReps() []uint32 {
	if m != nil {
		return m.SvcReps
	}
	return nil
}

func (m *RASEvent_PoolSvcEventInfo) GetVersion() uint64 {
	if m != nil {
		return m.Version
	}
	return 0
}

// ClusterEventReq communicates occurrence of a RAS event in the DAOS system.
type ClusterEventReq struct {
	Sequence             uint64    `protobuf:"varint,1,opt,name=sequence,proto3" json:"sequence,omitempty"`
	Event                *RASEvent `protobuf:"bytes,2,opt,name=event,proto3" json:"event,omitempty"`
	XXX_NoUnkeyedLiteral struct{}  `json:"-"`
	XXX_unrecognized     []byte    `json:"-"`
	XXX_sizecache        int32     `json:"-"`
}

func (m *ClusterEventReq) Reset()         { *m = ClusterEventReq{} }
func (m *ClusterEventReq) String() string { return proto.CompactTextString(m) }
func (*ClusterEventReq) ProtoMessage()    {}
func (*ClusterEventReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_462d23cf02562d79, []int{1}
}

func (m *ClusterEventReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ClusterEventReq.Unmarshal(m, b)
}
func (m *ClusterEventReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ClusterEventReq.Marshal(b, m, deterministic)
}
func (m *ClusterEventReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ClusterEventReq.Merge(m, src)
}
func (m *ClusterEventReq) XXX_Size() int {
	return xxx_messageInfo_ClusterEventReq.Size(m)
}
func (m *ClusterEventReq) XXX_DiscardUnknown() {
	xxx_messageInfo_ClusterEventReq.DiscardUnknown(m)
}

var xxx_messageInfo_ClusterEventReq proto.InternalMessageInfo

func (m *ClusterEventReq) GetSequence() uint64 {
	if m != nil {
		return m.Sequence
	}
	return 0
}

func (m *ClusterEventReq) GetEvent() *RASEvent {
	if m != nil {
		return m.Event
	}
	return nil
}

// RASEventResp acknowledges receipt of an event notification.
type ClusterEventResp struct {
	Sequence             uint64   `protobuf:"varint,1,opt,name=sequence,proto3" json:"sequence,omitempty"`
	Status               int32    `protobuf:"varint,2,opt,name=status,proto3" json:"status,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ClusterEventResp) Reset()         { *m = ClusterEventResp{} }
func (m *ClusterEventResp) String() string { return proto.CompactTextString(m) }
func (*ClusterEventResp) ProtoMessage()    {}
func (*ClusterEventResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_462d23cf02562d79, []int{2}
}

func (m *ClusterEventResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ClusterEventResp.Unmarshal(m, b)
}
func (m *ClusterEventResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ClusterEventResp.Marshal(b, m, deterministic)
}
func (m *ClusterEventResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ClusterEventResp.Merge(m, src)
}
func (m *ClusterEventResp) XXX_Size() int {
	return xxx_messageInfo_ClusterEventResp.Size(m)
}
func (m *ClusterEventResp) XXX_DiscardUnknown() {
	xxx_messageInfo_ClusterEventResp.DiscardUnknown(m)
}

var xxx_messageInfo_ClusterEventResp proto.InternalMessageInfo

func (m *ClusterEventResp) GetSequence() uint64 {
	if m != nil {
		return m.Sequence
	}
	return 0
}

func (m *ClusterEventResp) GetStatus() int32 {
	if m != nil {
		return m.Status
	}
	return 0
}

func init() {
	proto.RegisterType((*RASEvent)(nil), "shared.RASEvent")
	proto.RegisterType((*RASEvent_RankStateEventInfo)(nil), "shared.RASEvent.RankStateEventInfo")
	proto.RegisterType((*RASEvent_PoolSvcEventInfo)(nil), "shared.RASEvent.PoolSvcEventInfo")
	proto.RegisterType((*ClusterEventReq)(nil), "shared.ClusterEventReq")
	proto.RegisterType((*ClusterEventResp)(nil), "shared.ClusterEventResp")
}

func init() {
	proto.RegisterFile("shared/event.proto", fileDescriptor_462d23cf02562d79)
}

var fileDescriptor_462d23cf02562d79 = []byte{
	// 565 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x7c, 0x93, 0xc1, 0x6f, 0xd3, 0x3e,
	0x14, 0xc7, 0xd7, 0xad, 0x49, 0xdb, 0xd7, 0x5f, 0xd7, 0xfe, 0x0c, 0x03, 0xb3, 0x71, 0x28, 0x43,
	0x42, 0xbd, 0xd0, 0x48, 0xe3, 0x86, 0xb8, 0x30, 0x04, 0x5b, 0x0f, 0x08, 0xe4, 0x69, 0x17, 0x2e,
	0x25, 0xb1, 0xbd, 0x25, 0x5d, 0x62, 0x67, 0xb6, 0x93, 0xb1, 0x3f, 0x98, 0xff, 0x03, 0xf9, 0x79,
	0x2d, 0xb0, 0x49, 0xdc, 0xde, 0xf7, 0xfb, 0xb5, 0x3f, 0xb1, 0xf3, 0x9e, 0x81, 0xd8, 0x3c, 0x35,
	0x52, 0x24, 0xb2, 0x95, 0xca, 0xcd, 0x6b, 0xa3, 0x9d, 0x26, 0x71, 0xf0, 0x0e, 0x7f, 0x46, 0xd0,
	0x67, 0xef, 0xcf, 0x3e, 0xfa, 0x88, 0xec, 0xc2, 0x76, 0x21, 0x68, 0x67, 0xda, 0x99, 0x8d, 0xd8,
	0x76, 0x21, 0xc8, 0x04, 0x76, 0x2a, 0x7b, 0x49, 0xb7, 0xa7, 0x9d, 0xd9, 0x80, 0xf9, 0x92, 0x3c,
	0x87, 0x81, 0x2b, 0x2a, 0x69, 0x5d, 0x5a, 0xd5, 0x74, 0x07, 0xfd, 0xdf, 0x06, 0x21, 0xd0, 0x75,
	0xb7, 0xb5, 0xa4, 0x5d, 0x24, 0x60, 0x4d, 0xf6, 0xa1, 0x6f, 0x65, 0x2b, 0x4d, 0xe1, 0x6e, 0x69,
	0x84, 0xfe, 0x46, 0xfb, 0x2c, 0xd7, 0xd6, 0xa9, 0xb4, 0x92, 0x34, 0x46, 0xd8, 0x46, 0x7b, 0x96,
	0x49, 0xd5, 0x15, 0xed, 0x05, 0x96, 0xaf, 0xc9, 0x23, 0x88, 0xf2, 0x9b, 0x65, 0x21, 0x68, 0x1f,
	0x17, 0x77, 0xf3, 0x9b, 0x85, 0x20, 0x4f, 0xa1, 0x57, 0x1b, 0xcd, 0xbd, 0x3d, 0x40, 0x3b, 0xf6,
	0x72, 0x21, 0xc8, 0x01, 0x0c, 0x5c, 0x6e, 0x64, 0x2a, 0x7c, 0x04, 0x01, 0x1f, 0x8c, 0x85, 0x20,
	0x7b, 0x10, 0xaf, 0x74, 0xe6, 0x93, 0x21, 0x26, 0xd1, 0x4a, 0x67, 0x61, 0x4f, 0xad, 0x75, 0xb9,
	0x6c, 0x9a, 0x42, 0xd0, 0xff, 0xc2, 0x1e, 0x6f, 0x9c, 0x37, 0x05, 0x86, 0x5c, 0x2b, 0x17, 0xc2,
	0x51, 0x08, 0xbd, 0x81, 0xe1, 0x1e, 0xc4, 0x3a, 0x5b, 0x79, 0xe0, 0x6e, 0x00, 0xea, 0x6c, 0x15,
	0xbe, 0xc3, 0x5d, 0xb9, 0xd4, 0x35, 0x1d, 0x07, 0x9b, 0xbb, 0xf2, 0x4b, 0x4d, 0x0e, 0xa0, 0x6f,
	0x9d, 0x59, 0x16, 0xea, 0x42, 0xd3, 0x89, 0x0f, 0x4e, 0xb7, 0x58, 0xcf, 0x3a, 0xb3, 0x50, 0x17,
	0x9a, 0x7c, 0x86, 0xb1, 0xbf, 0xee, 0xd2, 0xba, 0xd4, 0xc9, 0xb0, 0xe6, 0xff, 0x69, 0x67, 0x36,
	0x3c, 0x7a, 0x39, 0x0f, 0x5d, 0x9b, 0xaf, 0x3b, 0x36, 0x67, 0xa9, 0xba, 0x3a, 0xf3, 0xcb, 0x50,
	0xfa, 0xdd, 0xa7, 0x5b, 0x6c, 0x64, 0xd6, 0x2e, 0xe2, 0x4e, 0x60, 0x84, 0x77, 0xb2, 0x2d, 0x0f,
	0x30, 0x82, 0xb0, 0x17, 0x0f, 0x60, 0x5f, 0xb5, 0x2e, 0xcf, 0x5a, 0xfe, 0x27, 0x6a, 0x58, 0x07,
	0xcf, 0xcb, 0xfd, 0xef, 0x40, 0x1e, 0x7e, 0xcf, 0x37, 0xb1, 0x50, 0xd6, 0xa5, 0x8a, 0xcb, 0xbb,
	0xd1, 0xd9, 0x68, 0x42, 0xa1, 0x27, 0x8d, 0xd1, 0x46, 0x0a, 0x1c, 0xa2, 0x3e, 0x5b, 0x4b, 0xf2,
	0x18, 0x22, 0x2c, 0xef, 0x86, 0x28, 0x88, 0xfd, 0x13, 0x98, 0xdc, 0x3f, 0x04, 0x79, 0x06, 0x7d,
	0x7f, 0x72, 0x23, 0x6b, 0x4b, 0x3b, 0xd3, 0x9d, 0xd9, 0x88, 0xf5, 0x6c, 0xcb, 0x99, 0xac, 0xad,
	0xc7, 0xb7, 0xd2, 0xd8, 0x42, 0x2b, 0xc4, 0x77, 0xd9, 0x5a, 0x1e, 0x8f, 0x61, 0x24, 0x7f, 0x38,
	0xa9, 0x84, 0x14, 0x78, 0xe7, 0xc3, 0x73, 0x18, 0x7f, 0x28, 0x1b, 0xeb, 0xa4, 0x41, 0x32, 0x93,
	0xd7, 0x61, 0x32, 0xaf, 0x1b, 0xb9, 0x3e, 0x78, 0x97, 0x6d, 0x34, 0x79, 0x05, 0x11, 0xbe, 0x16,
	0xe4, 0x0e, 0x8f, 0x26, 0xf7, 0xff, 0x15, 0x0b, 0xf1, 0xe1, 0x27, 0x98, 0xfc, 0x8d, 0xb5, 0xf5,
	0x3f, 0xb9, 0x4f, 0x20, 0xf6, 0x5d, 0x6d, 0x2c, 0x82, 0x23, 0x76, 0xa7, 0x8e, 0xdf, 0x7d, 0x7b,
	0x7b, 0x59, 0xb8, 0xbc, 0xc9, 0xe6, 0x5c, 0x57, 0x89, 0x48, 0xb5, 0x7d, 0x6d, 0x5d, 0xca, 0xaf,
	0xb0, 0x4c, 0xac, 0xe1, 0x89, 0x1f, 0x34, 0xa3, 0xcb, 0x84, 0xeb, 0xaa, 0xd2, 0x2a, 0xc1, 0x57,
	0x9c, 0x84, 0x53, 0x65, 0x31, 0xaa, 0x37, 0xbf, 0x02, 0x00, 0x00, 0xff, 0xff, 0xb1, 0x15, 0xaa,
	0x47, 0xe9, 0x03, 0x00, 0x00,
}
