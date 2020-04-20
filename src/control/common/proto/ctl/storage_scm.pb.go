// Code generated by protoc-gen-go. DO NOT EDIT.
// source: storage_scm.proto

package ctl

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

// ScmModule represent Storage Class Memory modules installed.
type ScmModule struct {
	Channelid            uint32   `protobuf:"varint,1,opt,name=channelid,proto3" json:"channelid,omitempty"`
	Channelposition      uint32   `protobuf:"varint,2,opt,name=channelposition,proto3" json:"channelposition,omitempty"`
	Controllerid         uint32   `protobuf:"varint,3,opt,name=controllerid,proto3" json:"controllerid,omitempty"`
	Socketid             uint32   `protobuf:"varint,4,opt,name=socketid,proto3" json:"socketid,omitempty"`
	Physicalid           uint32   `protobuf:"varint,5,opt,name=physicalid,proto3" json:"physicalid,omitempty"`
	Capacity             uint64   `protobuf:"varint,6,opt,name=capacity,proto3" json:"capacity,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ScmModule) Reset()         { *m = ScmModule{} }
func (m *ScmModule) String() string { return proto.CompactTextString(m) }
func (*ScmModule) ProtoMessage()    {}
func (*ScmModule) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{0}
}

func (m *ScmModule) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmModule.Unmarshal(m, b)
}
func (m *ScmModule) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmModule.Marshal(b, m, deterministic)
}
func (m *ScmModule) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmModule.Merge(m, src)
}
func (m *ScmModule) XXX_Size() int {
	return xxx_messageInfo_ScmModule.Size(m)
}
func (m *ScmModule) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmModule.DiscardUnknown(m)
}

var xxx_messageInfo_ScmModule proto.InternalMessageInfo

func (m *ScmModule) GetChannelid() uint32 {
	if m != nil {
		return m.Channelid
	}
	return 0
}

func (m *ScmModule) GetChannelposition() uint32 {
	if m != nil {
		return m.Channelposition
	}
	return 0
}

func (m *ScmModule) GetControllerid() uint32 {
	if m != nil {
		return m.Controllerid
	}
	return 0
}

func (m *ScmModule) GetSocketid() uint32 {
	if m != nil {
		return m.Socketid
	}
	return 0
}

func (m *ScmModule) GetPhysicalid() uint32 {
	if m != nil {
		return m.Physicalid
	}
	return 0
}

func (m *ScmModule) GetCapacity() uint64 {
	if m != nil {
		return m.Capacity
	}
	return 0
}

// ScmNamespace represents SCM namespace as pmem device files created on a ScmRegion.
type ScmNamespace struct {
	Uuid                 string   `protobuf:"bytes,1,opt,name=uuid,proto3" json:"uuid,omitempty"`
	Blockdev             string   `protobuf:"bytes,2,opt,name=blockdev,proto3" json:"blockdev,omitempty"`
	Dev                  string   `protobuf:"bytes,3,opt,name=dev,proto3" json:"dev,omitempty"`
	NumaNode             uint32   `protobuf:"varint,4,opt,name=numa_node,json=numaNode,proto3" json:"numa_node,omitempty"`
	Size                 uint64   `protobuf:"varint,5,opt,name=size,proto3" json:"size,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ScmNamespace) Reset()         { *m = ScmNamespace{} }
func (m *ScmNamespace) String() string { return proto.CompactTextString(m) }
func (*ScmNamespace) ProtoMessage()    {}
func (*ScmNamespace) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{1}
}

func (m *ScmNamespace) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmNamespace.Unmarshal(m, b)
}
func (m *ScmNamespace) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmNamespace.Marshal(b, m, deterministic)
}
func (m *ScmNamespace) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmNamespace.Merge(m, src)
}
func (m *ScmNamespace) XXX_Size() int {
	return xxx_messageInfo_ScmNamespace.Size(m)
}
func (m *ScmNamespace) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmNamespace.DiscardUnknown(m)
}

var xxx_messageInfo_ScmNamespace proto.InternalMessageInfo

func (m *ScmNamespace) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *ScmNamespace) GetBlockdev() string {
	if m != nil {
		return m.Blockdev
	}
	return ""
}

func (m *ScmNamespace) GetDev() string {
	if m != nil {
		return m.Dev
	}
	return ""
}

func (m *ScmNamespace) GetNumaNode() uint32 {
	if m != nil {
		return m.NumaNode
	}
	return 0
}

func (m *ScmNamespace) GetSize() uint64 {
	if m != nil {
		return m.Size
	}
	return 0
}

// ScmMount represents mounted AppDirect region made up of SCM module set.
type ScmMount struct {
	Mntpoint             string        `protobuf:"bytes,1,opt,name=mntpoint,proto3" json:"mntpoint,omitempty"`
	Modules              []*ScmModule  `protobuf:"bytes,2,rep,name=modules,proto3" json:"modules,omitempty"`
	Namespace            *ScmNamespace `protobuf:"bytes,3,opt,name=namespace,proto3" json:"namespace,omitempty"`
	XXX_NoUnkeyedLiteral struct{}      `json:"-"`
	XXX_unrecognized     []byte        `json:"-"`
	XXX_sizecache        int32         `json:"-"`
}

func (m *ScmMount) Reset()         { *m = ScmMount{} }
func (m *ScmMount) String() string { return proto.CompactTextString(m) }
func (*ScmMount) ProtoMessage()    {}
func (*ScmMount) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{2}
}

func (m *ScmMount) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmMount.Unmarshal(m, b)
}
func (m *ScmMount) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmMount.Marshal(b, m, deterministic)
}
func (m *ScmMount) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmMount.Merge(m, src)
}
func (m *ScmMount) XXX_Size() int {
	return xxx_messageInfo_ScmMount.Size(m)
}
func (m *ScmMount) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmMount.DiscardUnknown(m)
}

var xxx_messageInfo_ScmMount proto.InternalMessageInfo

func (m *ScmMount) GetMntpoint() string {
	if m != nil {
		return m.Mntpoint
	}
	return ""
}

func (m *ScmMount) GetModules() []*ScmModule {
	if m != nil {
		return m.Modules
	}
	return nil
}

func (m *ScmMount) GetNamespace() *ScmNamespace {
	if m != nil {
		return m.Namespace
	}
	return nil
}

// ScmModuleResult represents operation state for specific SCM/PM module.
//
// TODO: replace identifier with serial when returned in scan
type ScmModuleResult struct {
	Physicalid           uint32         `protobuf:"varint,1,opt,name=physicalid,proto3" json:"physicalid,omitempty"`
	State                *ResponseState `protobuf:"bytes,2,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *ScmModuleResult) Reset()         { *m = ScmModuleResult{} }
func (m *ScmModuleResult) String() string { return proto.CompactTextString(m) }
func (*ScmModuleResult) ProtoMessage()    {}
func (*ScmModuleResult) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{3}
}

func (m *ScmModuleResult) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmModuleResult.Unmarshal(m, b)
}
func (m *ScmModuleResult) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmModuleResult.Marshal(b, m, deterministic)
}
func (m *ScmModuleResult) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmModuleResult.Merge(m, src)
}
func (m *ScmModuleResult) XXX_Size() int {
	return xxx_messageInfo_ScmModuleResult.Size(m)
}
func (m *ScmModuleResult) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmModuleResult.DiscardUnknown(m)
}

var xxx_messageInfo_ScmModuleResult proto.InternalMessageInfo

func (m *ScmModuleResult) GetPhysicalid() uint32 {
	if m != nil {
		return m.Physicalid
	}
	return 0
}

func (m *ScmModuleResult) GetState() *ResponseState {
	if m != nil {
		return m.State
	}
	return nil
}

// ScmMountResult represents operation state for specific SCM mount point.
type ScmMountResult struct {
	Mntpoint             string         `protobuf:"bytes,1,opt,name=mntpoint,proto3" json:"mntpoint,omitempty"`
	State                *ResponseState `protobuf:"bytes,2,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *ScmMountResult) Reset()         { *m = ScmMountResult{} }
func (m *ScmMountResult) String() string { return proto.CompactTextString(m) }
func (*ScmMountResult) ProtoMessage()    {}
func (*ScmMountResult) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{4}
}

func (m *ScmMountResult) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmMountResult.Unmarshal(m, b)
}
func (m *ScmMountResult) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmMountResult.Marshal(b, m, deterministic)
}
func (m *ScmMountResult) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmMountResult.Merge(m, src)
}
func (m *ScmMountResult) XXX_Size() int {
	return xxx_messageInfo_ScmMountResult.Size(m)
}
func (m *ScmMountResult) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmMountResult.DiscardUnknown(m)
}

var xxx_messageInfo_ScmMountResult proto.InternalMessageInfo

func (m *ScmMountResult) GetMntpoint() string {
	if m != nil {
		return m.Mntpoint
	}
	return ""
}

func (m *ScmMountResult) GetState() *ResponseState {
	if m != nil {
		return m.State
	}
	return nil
}

type PrepareScmReq struct {
	Reset_               bool     `protobuf:"varint,1,opt,name=reset,proto3" json:"reset,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *PrepareScmReq) Reset()         { *m = PrepareScmReq{} }
func (m *PrepareScmReq) String() string { return proto.CompactTextString(m) }
func (*PrepareScmReq) ProtoMessage()    {}
func (*PrepareScmReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{5}
}

func (m *PrepareScmReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PrepareScmReq.Unmarshal(m, b)
}
func (m *PrepareScmReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PrepareScmReq.Marshal(b, m, deterministic)
}
func (m *PrepareScmReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PrepareScmReq.Merge(m, src)
}
func (m *PrepareScmReq) XXX_Size() int {
	return xxx_messageInfo_PrepareScmReq.Size(m)
}
func (m *PrepareScmReq) XXX_DiscardUnknown() {
	xxx_messageInfo_PrepareScmReq.DiscardUnknown(m)
}

var xxx_messageInfo_PrepareScmReq proto.InternalMessageInfo

func (m *PrepareScmReq) GetReset_() bool {
	if m != nil {
		return m.Reset_
	}
	return false
}

type PrepareScmResp struct {
	Namespaces           []*ScmNamespace `protobuf:"bytes,1,rep,name=namespaces,proto3" json:"namespaces,omitempty"`
	State                *ResponseState  `protobuf:"bytes,2,opt,name=state,proto3" json:"state,omitempty"`
	Rebootrequired       bool            `protobuf:"varint,3,opt,name=rebootrequired,proto3" json:"rebootrequired,omitempty"`
	XXX_NoUnkeyedLiteral struct{}        `json:"-"`
	XXX_unrecognized     []byte          `json:"-"`
	XXX_sizecache        int32           `json:"-"`
}

func (m *PrepareScmResp) Reset()         { *m = PrepareScmResp{} }
func (m *PrepareScmResp) String() string { return proto.CompactTextString(m) }
func (*PrepareScmResp) ProtoMessage()    {}
func (*PrepareScmResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{6}
}

func (m *PrepareScmResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PrepareScmResp.Unmarshal(m, b)
}
func (m *PrepareScmResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PrepareScmResp.Marshal(b, m, deterministic)
}
func (m *PrepareScmResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PrepareScmResp.Merge(m, src)
}
func (m *PrepareScmResp) XXX_Size() int {
	return xxx_messageInfo_PrepareScmResp.Size(m)
}
func (m *PrepareScmResp) XXX_DiscardUnknown() {
	xxx_messageInfo_PrepareScmResp.DiscardUnknown(m)
}

var xxx_messageInfo_PrepareScmResp proto.InternalMessageInfo

func (m *PrepareScmResp) GetNamespaces() []*ScmNamespace {
	if m != nil {
		return m.Namespaces
	}
	return nil
}

func (m *PrepareScmResp) GetState() *ResponseState {
	if m != nil {
		return m.State
	}
	return nil
}

func (m *PrepareScmResp) GetRebootrequired() bool {
	if m != nil {
		return m.Rebootrequired
	}
	return false
}

type ScanScmReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ScanScmReq) Reset()         { *m = ScanScmReq{} }
func (m *ScanScmReq) String() string { return proto.CompactTextString(m) }
func (*ScanScmReq) ProtoMessage()    {}
func (*ScanScmReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{7}
}

func (m *ScanScmReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScanScmReq.Unmarshal(m, b)
}
func (m *ScanScmReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScanScmReq.Marshal(b, m, deterministic)
}
func (m *ScanScmReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScanScmReq.Merge(m, src)
}
func (m *ScanScmReq) XXX_Size() int {
	return xxx_messageInfo_ScanScmReq.Size(m)
}
func (m *ScanScmReq) XXX_DiscardUnknown() {
	xxx_messageInfo_ScanScmReq.DiscardUnknown(m)
}

var xxx_messageInfo_ScanScmReq proto.InternalMessageInfo

type ScanScmResp struct {
	Modules              []*ScmModule    `protobuf:"bytes,1,rep,name=modules,proto3" json:"modules,omitempty"`
	Namespaces           []*ScmNamespace `protobuf:"bytes,2,rep,name=namespaces,proto3" json:"namespaces,omitempty"`
	State                *ResponseState  `protobuf:"bytes,3,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}        `json:"-"`
	XXX_unrecognized     []byte          `json:"-"`
	XXX_sizecache        int32           `json:"-"`
}

func (m *ScanScmResp) Reset()         { *m = ScanScmResp{} }
func (m *ScanScmResp) String() string { return proto.CompactTextString(m) }
func (*ScanScmResp) ProtoMessage()    {}
func (*ScanScmResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{8}
}

func (m *ScanScmResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScanScmResp.Unmarshal(m, b)
}
func (m *ScanScmResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScanScmResp.Marshal(b, m, deterministic)
}
func (m *ScanScmResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScanScmResp.Merge(m, src)
}
func (m *ScanScmResp) XXX_Size() int {
	return xxx_messageInfo_ScanScmResp.Size(m)
}
func (m *ScanScmResp) XXX_DiscardUnknown() {
	xxx_messageInfo_ScanScmResp.DiscardUnknown(m)
}

var xxx_messageInfo_ScanScmResp proto.InternalMessageInfo

func (m *ScanScmResp) GetModules() []*ScmModule {
	if m != nil {
		return m.Modules
	}
	return nil
}

func (m *ScanScmResp) GetNamespaces() []*ScmNamespace {
	if m != nil {
		return m.Namespaces
	}
	return nil
}

func (m *ScanScmResp) GetState() *ResponseState {
	if m != nil {
		return m.State
	}
	return nil
}

type FormatScmReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *FormatScmReq) Reset()         { *m = FormatScmReq{} }
func (m *FormatScmReq) String() string { return proto.CompactTextString(m) }
func (*FormatScmReq) ProtoMessage()    {}
func (*FormatScmReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_fa79a1cba4dc284c, []int{9}
}

func (m *FormatScmReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FormatScmReq.Unmarshal(m, b)
}
func (m *FormatScmReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FormatScmReq.Marshal(b, m, deterministic)
}
func (m *FormatScmReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FormatScmReq.Merge(m, src)
}
func (m *FormatScmReq) XXX_Size() int {
	return xxx_messageInfo_FormatScmReq.Size(m)
}
func (m *FormatScmReq) XXX_DiscardUnknown() {
	xxx_messageInfo_FormatScmReq.DiscardUnknown(m)
}

var xxx_messageInfo_FormatScmReq proto.InternalMessageInfo

func init() {
	proto.RegisterType((*ScmModule)(nil), "ctl.ScmModule")
	proto.RegisterType((*ScmNamespace)(nil), "ctl.ScmNamespace")
	proto.RegisterType((*ScmMount)(nil), "ctl.ScmMount")
	proto.RegisterType((*ScmModuleResult)(nil), "ctl.ScmModuleResult")
	proto.RegisterType((*ScmMountResult)(nil), "ctl.ScmMountResult")
	proto.RegisterType((*PrepareScmReq)(nil), "ctl.PrepareScmReq")
	proto.RegisterType((*PrepareScmResp)(nil), "ctl.PrepareScmResp")
	proto.RegisterType((*ScanScmReq)(nil), "ctl.ScanScmReq")
	proto.RegisterType((*ScanScmResp)(nil), "ctl.ScanScmResp")
	proto.RegisterType((*FormatScmReq)(nil), "ctl.FormatScmReq")
}

func init() {
	proto.RegisterFile("storage_scm.proto", fileDescriptor_fa79a1cba4dc284c)
}

var fileDescriptor_fa79a1cba4dc284c = []byte{
	// 479 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x94, 0x54, 0xdd, 0x6e, 0xd3, 0x30,
	0x18, 0x95, 0x9b, 0x76, 0x34, 0x5f, 0xbb, 0x8c, 0x59, 0x5c, 0x44, 0x03, 0xa1, 0x2a, 0x12, 0x28,
	0x57, 0x45, 0x94, 0x77, 0xe0, 0x8e, 0x09, 0x39, 0x12, 0x37, 0x5c, 0x4c, 0xae, 0xf3, 0x89, 0x59,
	0x8b, 0x7f, 0x66, 0x3b, 0x48, 0xe3, 0x8e, 0x77, 0x80, 0xe7, 0xe2, 0x95, 0x50, 0x9c, 0x26, 0xed,
	0x0a, 0x1a, 0xe3, 0xce, 0xdf, 0xf9, 0x4e, 0x4e, 0xce, 0x39, 0x8e, 0x02, 0xe7, 0x3e, 0x18, 0xc7,
	0xbf, 0xe0, 0x95, 0x17, 0x6a, 0x6d, 0x9d, 0x09, 0x86, 0x26, 0x22, 0x34, 0x17, 0x4b, 0x61, 0x94,
	0x32, 0xba, 0x87, 0x8a, 0x5f, 0x04, 0xd2, 0x4a, 0xa8, 0x0f, 0xa6, 0x6e, 0x1b, 0xa4, 0x2f, 0x20,
	0x15, 0xd7, 0x5c, 0x6b, 0x6c, 0x64, 0x9d, 0x93, 0x15, 0x29, 0x4f, 0xd9, 0x1e, 0xa0, 0x25, 0x9c,
	0xed, 0x06, 0x6b, 0xbc, 0x0c, 0xd2, 0xe8, 0x7c, 0x12, 0x39, 0xc7, 0x30, 0x2d, 0x60, 0x29, 0x8c,
	0x0e, 0xce, 0x34, 0x0d, 0x3a, 0x59, 0xe7, 0x49, 0xa4, 0xdd, 0xc3, 0xe8, 0x05, 0xcc, 0xbd, 0x11,
	0x37, 0x18, 0x64, 0x9d, 0x4f, 0xe3, 0x7e, 0x9c, 0xe9, 0x4b, 0x00, 0x7b, 0x7d, 0xe7, 0xa5, 0xe0,
	0x9d, 0x91, 0x59, 0xdc, 0x1e, 0x20, 0xdd, 0xb3, 0x82, 0x5b, 0x2e, 0x64, 0xb8, 0xcb, 0x4f, 0x56,
	0xa4, 0x9c, 0xb2, 0x71, 0x2e, 0xbe, 0x13, 0x58, 0x56, 0x42, 0x5d, 0x72, 0x85, 0xde, 0x72, 0x81,
	0x94, 0xc2, 0xb4, 0x6d, 0x77, 0x79, 0x52, 0x16, 0xcf, 0x9d, 0xc0, 0xb6, 0x31, 0xe2, 0xa6, 0xc6,
	0xaf, 0x31, 0x43, 0xca, 0xc6, 0x99, 0x3e, 0x85, 0xa4, 0x83, 0x93, 0x08, 0x77, 0x47, 0xfa, 0x1c,
	0x52, 0xdd, 0x2a, 0x7e, 0xa5, 0x4d, 0x8d, 0x83, 0xd7, 0x0e, 0xb8, 0x34, 0x75, 0x94, 0xf7, 0xf2,
	0x1b, 0x46, 0x97, 0x53, 0x16, 0xcf, 0x9d, 0x87, 0x79, 0x6c, 0xb5, 0xd5, 0xa1, 0x7b, 0x97, 0xd2,
	0xc1, 0x1a, 0xa9, 0xc3, 0xce, 0xc3, 0x38, 0xd3, 0x12, 0x9e, 0xa8, 0x58, 0xbd, 0xcf, 0x27, 0xab,
	0xa4, 0x5c, 0x6c, 0xb2, 0xb5, 0x08, 0xcd, 0x7a, 0xbc, 0x11, 0x36, 0xac, 0xe9, 0x1b, 0x48, 0xf5,
	0x10, 0x29, 0x7a, 0x5b, 0x6c, 0xce, 0x07, 0xee, 0x98, 0x95, 0xed, 0x39, 0xc5, 0x67, 0x38, 0xdb,
	0xcb, 0xa0, 0x6f, 0x9b, 0x70, 0x54, 0x2b, 0xf9, 0xa3, 0xd6, 0x12, 0x66, 0x3e, 0xf0, 0x80, 0xb1,
	0x92, 0xc5, 0x86, 0x46, 0x7d, 0x86, 0xde, 0x1a, 0xed, 0xb1, 0xea, 0x36, 0xac, 0x27, 0x14, 0x9f,
	0x20, 0x1b, 0xf2, 0xed, 0xb4, 0x1f, 0x4e, 0xf9, 0x58, 0xdd, 0x57, 0x70, 0xfa, 0xd1, 0xa1, 0xe5,
	0x0e, 0x2b, 0xa1, 0x18, 0xde, 0xd2, 0x67, 0x30, 0x73, 0xe8, 0xb1, 0xd7, 0x9c, 0xb3, 0x7e, 0x28,
	0x7e, 0x12, 0xc8, 0x0e, 0x79, 0xde, 0xd2, 0xb7, 0x00, 0x63, 0x76, 0x9f, 0x93, 0x58, 0xe6, 0x5f,
	0x0a, 0x3a, 0x20, 0x3d, 0xde, 0x16, 0x7d, 0x0d, 0x99, 0xc3, 0xad, 0x31, 0xc1, 0xe1, 0x6d, 0x2b,
	0x1d, 0xf6, 0x5f, 0xf4, 0x9c, 0x1d, 0xa1, 0xc5, 0x12, 0xa0, 0x12, 0x5c, 0xf7, 0xde, 0x8b, 0x1f,
	0x04, 0x16, 0xe3, 0xe8, 0xed, 0xe1, 0x65, 0x93, 0x87, 0x2f, 0xfb, 0x7e, 0x98, 0xc9, 0x7f, 0x85,
	0x49, 0xfe, 0xd5, 0x71, 0x06, 0xcb, 0xf7, 0xc6, 0x29, 0x1e, 0x7a, 0x9b, 0xdb, 0x93, 0xf8, 0x27,
	0x78, 0xf7, 0x3b, 0x00, 0x00, 0xff, 0xff, 0x07, 0x5c, 0x2b, 0x61, 0x31, 0x04, 0x00, 0x00,
}
