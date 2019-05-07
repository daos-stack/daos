// Code generated by protoc-gen-go. DO NOT EDIT.
// source: storage_scm.proto

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

// ScmModule represent Storage Class Memory modules installed.
type ScmModule struct {
	// string uid = 1; // The uid of the module.
	Physicalid uint32 `protobuf:"varint,1,opt,name=physicalid,proto3" json:"physicalid,omitempty"`
	// string handle = 3; // The device handle of the module.
	// string serial = 8; // The serial number of the module.
	Capacity uint64 `protobuf:"varint,2,opt,name=capacity,proto3" json:"capacity,omitempty"`
	// string fwrev = 10; // The firmware revision of the module.
	Loc                  *ScmModule_Location `protobuf:"bytes,3,opt,name=loc,proto3" json:"loc,omitempty"`
	XXX_NoUnkeyedLiteral struct{}            `json:"-"`
	XXX_unrecognized     []byte              `json:"-"`
	XXX_sizecache        int32               `json:"-"`
}

func (m *ScmModule) Reset()         { *m = ScmModule{} }
func (m *ScmModule) String() string { return proto.CompactTextString(m) }
func (*ScmModule) ProtoMessage()    {}
func (*ScmModule) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{0}
}
func (m *ScmModule) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmModule.Unmarshal(m, b)
}
func (m *ScmModule) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmModule.Marshal(b, m, deterministic)
}
func (dst *ScmModule) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmModule.Merge(dst, src)
}
func (m *ScmModule) XXX_Size() int {
	return xxx_messageInfo_ScmModule.Size(m)
}
func (m *ScmModule) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmModule.DiscardUnknown(m)
}

var xxx_messageInfo_ScmModule proto.InternalMessageInfo

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

func (m *ScmModule) GetLoc() *ScmModule_Location {
	if m != nil {
		return m.Loc
	}
	return nil
}

type ScmModule_Location struct {
	Channel              uint32   `protobuf:"varint,1,opt,name=channel,proto3" json:"channel,omitempty"`
	Channelpos           uint32   `protobuf:"varint,2,opt,name=channelpos,proto3" json:"channelpos,omitempty"`
	Memctrlr             uint32   `protobuf:"varint,3,opt,name=memctrlr,proto3" json:"memctrlr,omitempty"`
	Socket               uint32   `protobuf:"varint,4,opt,name=socket,proto3" json:"socket,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ScmModule_Location) Reset()         { *m = ScmModule_Location{} }
func (m *ScmModule_Location) String() string { return proto.CompactTextString(m) }
func (*ScmModule_Location) ProtoMessage()    {}
func (*ScmModule_Location) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{0, 0}
}
func (m *ScmModule_Location) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmModule_Location.Unmarshal(m, b)
}
func (m *ScmModule_Location) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmModule_Location.Marshal(b, m, deterministic)
}
func (dst *ScmModule_Location) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmModule_Location.Merge(dst, src)
}
func (m *ScmModule_Location) XXX_Size() int {
	return xxx_messageInfo_ScmModule_Location.Size(m)
}
func (m *ScmModule_Location) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmModule_Location.DiscardUnknown(m)
}

var xxx_messageInfo_ScmModule_Location proto.InternalMessageInfo

func (m *ScmModule_Location) GetChannel() uint32 {
	if m != nil {
		return m.Channel
	}
	return 0
}

func (m *ScmModule_Location) GetChannelpos() uint32 {
	if m != nil {
		return m.Channelpos
	}
	return 0
}

func (m *ScmModule_Location) GetMemctrlr() uint32 {
	if m != nil {
		return m.Memctrlr
	}
	return 0
}

func (m *ScmModule_Location) GetSocket() uint32 {
	if m != nil {
		return m.Socket
	}
	return 0
}

// ScmMount represents mounted AppDirect region made up of SCM module set.
type ScmMount struct {
	Mntpoint             string       `protobuf:"bytes,1,opt,name=mntpoint,proto3" json:"mntpoint,omitempty"`
	Modules              []*ScmModule `protobuf:"bytes,2,rep,name=modules,proto3" json:"modules,omitempty"`
	XXX_NoUnkeyedLiteral struct{}     `json:"-"`
	XXX_unrecognized     []byte       `json:"-"`
	XXX_sizecache        int32        `json:"-"`
}

func (m *ScmMount) Reset()         { *m = ScmMount{} }
func (m *ScmMount) String() string { return proto.CompactTextString(m) }
func (*ScmMount) ProtoMessage()    {}
func (*ScmMount) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{1}
}
func (m *ScmMount) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmMount.Unmarshal(m, b)
}
func (m *ScmMount) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmMount.Marshal(b, m, deterministic)
}
func (dst *ScmMount) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmMount.Merge(dst, src)
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

// ScmModuleResult represents operation state for specific SCM/PM module.
//
// TODO: replace identifier with serial when returned in scan
type ScmModuleResult struct {
	Loc                  *ScmModule_Location `protobuf:"bytes,1,opt,name=loc,proto3" json:"loc,omitempty"`
	State                *ResponseState      `protobuf:"bytes,2,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}            `json:"-"`
	XXX_unrecognized     []byte              `json:"-"`
	XXX_sizecache        int32               `json:"-"`
}

func (m *ScmModuleResult) Reset()         { *m = ScmModuleResult{} }
func (m *ScmModuleResult) String() string { return proto.CompactTextString(m) }
func (*ScmModuleResult) ProtoMessage()    {}
func (*ScmModuleResult) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{2}
}
func (m *ScmModuleResult) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmModuleResult.Unmarshal(m, b)
}
func (m *ScmModuleResult) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmModuleResult.Marshal(b, m, deterministic)
}
func (dst *ScmModuleResult) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmModuleResult.Merge(dst, src)
}
func (m *ScmModuleResult) XXX_Size() int {
	return xxx_messageInfo_ScmModuleResult.Size(m)
}
func (m *ScmModuleResult) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmModuleResult.DiscardUnknown(m)
}

var xxx_messageInfo_ScmModuleResult proto.InternalMessageInfo

func (m *ScmModuleResult) GetLoc() *ScmModule_Location {
	if m != nil {
		return m.Loc
	}
	return nil
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
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{3}
}
func (m *ScmMountResult) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmMountResult.Unmarshal(m, b)
}
func (m *ScmMountResult) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmMountResult.Marshal(b, m, deterministic)
}
func (dst *ScmMountResult) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmMountResult.Merge(dst, src)
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

type ScanScmParams struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *ScanScmParams) Reset()         { *m = ScanScmParams{} }
func (m *ScanScmParams) String() string { return proto.CompactTextString(m) }
func (*ScanScmParams) ProtoMessage()    {}
func (*ScanScmParams) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{4}
}
func (m *ScanScmParams) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScanScmParams.Unmarshal(m, b)
}
func (m *ScanScmParams) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScanScmParams.Marshal(b, m, deterministic)
}
func (dst *ScanScmParams) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScanScmParams.Merge(dst, src)
}
func (m *ScanScmParams) XXX_Size() int {
	return xxx_messageInfo_ScanScmParams.Size(m)
}
func (m *ScanScmParams) XXX_DiscardUnknown() {
	xxx_messageInfo_ScanScmParams.DiscardUnknown(m)
}

var xxx_messageInfo_ScanScmParams proto.InternalMessageInfo

type FormatScmParams struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *FormatScmParams) Reset()         { *m = FormatScmParams{} }
func (m *FormatScmParams) String() string { return proto.CompactTextString(m) }
func (*FormatScmParams) ProtoMessage()    {}
func (*FormatScmParams) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{5}
}
func (m *FormatScmParams) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FormatScmParams.Unmarshal(m, b)
}
func (m *FormatScmParams) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FormatScmParams.Marshal(b, m, deterministic)
}
func (dst *FormatScmParams) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FormatScmParams.Merge(dst, src)
}
func (m *FormatScmParams) XXX_Size() int {
	return xxx_messageInfo_FormatScmParams.Size(m)
}
func (m *FormatScmParams) XXX_DiscardUnknown() {
	xxx_messageInfo_FormatScmParams.DiscardUnknown(m)
}

var xxx_messageInfo_FormatScmParams proto.InternalMessageInfo

type UpdateScmParams struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *UpdateScmParams) Reset()         { *m = UpdateScmParams{} }
func (m *UpdateScmParams) String() string { return proto.CompactTextString(m) }
func (*UpdateScmParams) ProtoMessage()    {}
func (*UpdateScmParams) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{6}
}
func (m *UpdateScmParams) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_UpdateScmParams.Unmarshal(m, b)
}
func (m *UpdateScmParams) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_UpdateScmParams.Marshal(b, m, deterministic)
}
func (dst *UpdateScmParams) XXX_Merge(src proto.Message) {
	xxx_messageInfo_UpdateScmParams.Merge(dst, src)
}
func (m *UpdateScmParams) XXX_Size() int {
	return xxx_messageInfo_UpdateScmParams.Size(m)
}
func (m *UpdateScmParams) XXX_DiscardUnknown() {
	xxx_messageInfo_UpdateScmParams.DiscardUnknown(m)
}

var xxx_messageInfo_UpdateScmParams proto.InternalMessageInfo

type BurninScmParams struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *BurninScmParams) Reset()         { *m = BurninScmParams{} }
func (m *BurninScmParams) String() string { return proto.CompactTextString(m) }
func (*BurninScmParams) ProtoMessage()    {}
func (*BurninScmParams) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_4e39efe68fdda593, []int{7}
}
func (m *BurninScmParams) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_BurninScmParams.Unmarshal(m, b)
}
func (m *BurninScmParams) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_BurninScmParams.Marshal(b, m, deterministic)
}
func (dst *BurninScmParams) XXX_Merge(src proto.Message) {
	xxx_messageInfo_BurninScmParams.Merge(dst, src)
}
func (m *BurninScmParams) XXX_Size() int {
	return xxx_messageInfo_BurninScmParams.Size(m)
}
func (m *BurninScmParams) XXX_DiscardUnknown() {
	xxx_messageInfo_BurninScmParams.DiscardUnknown(m)
}

var xxx_messageInfo_BurninScmParams proto.InternalMessageInfo

func init() {
	proto.RegisterType((*ScmModule)(nil), "mgmt.ScmModule")
	proto.RegisterType((*ScmModule_Location)(nil), "mgmt.ScmModule.Location")
	proto.RegisterType((*ScmMount)(nil), "mgmt.ScmMount")
	proto.RegisterType((*ScmModuleResult)(nil), "mgmt.ScmModuleResult")
	proto.RegisterType((*ScmMountResult)(nil), "mgmt.ScmMountResult")
	proto.RegisterType((*ScanScmParams)(nil), "mgmt.ScanScmParams")
	proto.RegisterType((*FormatScmParams)(nil), "mgmt.FormatScmParams")
	proto.RegisterType((*UpdateScmParams)(nil), "mgmt.UpdateScmParams")
	proto.RegisterType((*BurninScmParams)(nil), "mgmt.BurninScmParams")
}

func init() { proto.RegisterFile("storage_scm.proto", fileDescriptor_storage_scm_4e39efe68fdda593) }

var fileDescriptor_storage_scm_4e39efe68fdda593 = []byte{
	// 341 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x8c, 0x92, 0xcf, 0x6a, 0xe3, 0x30,
	0x10, 0xc6, 0xf1, 0x26, 0x9b, 0x3f, 0x93, 0xf5, 0x9a, 0x68, 0x61, 0x31, 0x39, 0x94, 0xe0, 0x93,
	0xd3, 0x83, 0x0f, 0xe9, 0x1b, 0xf4, 0xd0, 0x53, 0x0b, 0xad, 0x4c, 0xe9, 0xb1, 0xa8, 0x8a, 0x48,
	0x4c, 0x2d, 0x8d, 0x91, 0xc6, 0xd0, 0x3c, 0x74, 0xdf, 0xa1, 0x58, 0xb1, 0x9d, 0xa4, 0x87, 0x92,
	0x9b, 0xbf, 0x6f, 0x46, 0xf3, 0xfb, 0x66, 0x30, 0xcc, 0x1d, 0xa1, 0x15, 0x5b, 0xf5, 0xea, 0xa4,
	0xce, 0x2a, 0x8b, 0x84, 0x6c, 0xa8, 0xb7, 0x9a, 0x16, 0x7f, 0x24, 0x6a, 0x8d, 0xe6, 0xe0, 0x25,
	0x9f, 0x01, 0x4c, 0x73, 0xa9, 0x1f, 0x70, 0x53, 0x97, 0x8a, 0x5d, 0x01, 0x54, 0xbb, 0xbd, 0x2b,
	0xa4, 0x28, 0x8b, 0x4d, 0x1c, 0x2c, 0x83, 0x34, 0xe4, 0x27, 0x0e, 0x5b, 0xc0, 0x44, 0x8a, 0x4a,
	0xc8, 0x82, 0xf6, 0xf1, 0xaf, 0x65, 0x90, 0x0e, 0x79, 0xaf, 0xd9, 0x35, 0x0c, 0x4a, 0x94, 0xf1,
	0x60, 0x19, 0xa4, 0xb3, 0x75, 0x9c, 0x35, 0xac, 0xac, 0x9f, 0x9c, 0xdd, 0xa3, 0x14, 0x54, 0xa0,
	0xe1, 0x4d, 0xd3, 0xe2, 0x03, 0x26, 0x9d, 0xc1, 0x62, 0x18, 0xcb, 0x9d, 0x30, 0x46, 0x95, 0x2d,
	0xb0, 0x93, 0x4d, 0x9a, 0xf6, 0xb3, 0x42, 0xe7, 0x79, 0x21, 0x3f, 0x71, 0x9a, 0x34, 0x5a, 0x69,
	0x49, 0xb6, 0xb4, 0x1e, 0x1b, 0xf2, 0x5e, 0xb3, 0xff, 0x30, 0x72, 0x28, 0xdf, 0x15, 0xc5, 0x43,
	0x5f, 0x69, 0x55, 0xf2, 0x04, 0x13, 0x1f, 0xaa, 0x36, 0xe4, 0xdf, 0x1b, 0xaa, 0xb0, 0x30, 0xe4,
	0xd1, 0x53, 0xde, 0x6b, 0xb6, 0x82, 0xb1, 0xf6, 0xc9, 0x1b, 0xf0, 0x20, 0x9d, 0xad, 0xa3, 0x6f,
	0x1b, 0xf1, 0xae, 0x9e, 0xec, 0x20, 0x3a, 0xba, 0xca, 0xd5, 0x25, 0x75, 0xb7, 0x08, 0x2e, 0xb8,
	0x05, 0x5b, 0xc1, 0x6f, 0x47, 0x82, 0x94, 0x5f, 0x70, 0xb6, 0xfe, 0x77, 0xe8, 0xe6, 0xca, 0x55,
	0x68, 0x9c, 0xca, 0x9b, 0x12, 0x3f, 0x74, 0x24, 0x2f, 0xf0, 0xb7, 0x0b, 0xdf, 0x82, 0x7e, 0x5e,
	0xe1, 0xe2, 0xc1, 0x11, 0x84, 0xb9, 0x14, 0x26, 0x97, 0xfa, 0x51, 0x58, 0xa1, 0x5d, 0x32, 0x87,
	0xe8, 0x0e, 0xad, 0x16, 0x74, 0x66, 0x3d, 0x57, 0x1b, 0x41, 0xea, 0xcc, 0xba, 0xad, 0xad, 0x29,
	0x8e, 0x0f, 0xdf, 0x46, 0xfe, 0xb7, 0xba, 0xf9, 0x0a, 0x00, 0x00, 0xff, 0xff, 0xf5, 0x06, 0x57,
	0x81, 0x7f, 0x02, 0x00, 0x00,
}
