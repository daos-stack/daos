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
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{0}
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
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{0, 0}
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

// PmemDevice represents SCM namespace as pmem kernel device created on a ScmRegion.
type PmemDevice struct {
	Uuid                 string   `protobuf:"bytes,1,opt,name=uuid,proto3" json:"uuid,omitempty"`
	Blockdev             string   `protobuf:"bytes,2,opt,name=blockdev,proto3" json:"blockdev,omitempty"`
	Dev                  string   `protobuf:"bytes,3,opt,name=dev,proto3" json:"dev,omitempty"`
	Numanode             uint32   `protobuf:"varint,4,opt,name=numanode,proto3" json:"numanode,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *PmemDevice) Reset()         { *m = PmemDevice{} }
func (m *PmemDevice) String() string { return proto.CompactTextString(m) }
func (*PmemDevice) ProtoMessage()    {}
func (*PmemDevice) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{1}
}
func (m *PmemDevice) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PmemDevice.Unmarshal(m, b)
}
func (m *PmemDevice) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PmemDevice.Marshal(b, m, deterministic)
}
func (dst *PmemDevice) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PmemDevice.Merge(dst, src)
}
func (m *PmemDevice) XXX_Size() int {
	return xxx_messageInfo_PmemDevice.Size(m)
}
func (m *PmemDevice) XXX_DiscardUnknown() {
	xxx_messageInfo_PmemDevice.DiscardUnknown(m)
}

var xxx_messageInfo_PmemDevice proto.InternalMessageInfo

func (m *PmemDevice) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *PmemDevice) GetBlockdev() string {
	if m != nil {
		return m.Blockdev
	}
	return ""
}

func (m *PmemDevice) GetDev() string {
	if m != nil {
		return m.Dev
	}
	return ""
}

func (m *PmemDevice) GetNumanode() uint32 {
	if m != nil {
		return m.Numanode
	}
	return 0
}

// ScmMount represents mounted AppDirect region made up of SCM module set.
type ScmMount struct {
	Mntpoint             string       `protobuf:"bytes,1,opt,name=mntpoint,proto3" json:"mntpoint,omitempty"`
	Modules              []*ScmModule `protobuf:"bytes,2,rep,name=modules,proto3" json:"modules,omitempty"`
	Pmem                 *PmemDevice  `protobuf:"bytes,3,opt,name=pmem,proto3" json:"pmem,omitempty"`
	XXX_NoUnkeyedLiteral struct{}     `json:"-"`
	XXX_unrecognized     []byte       `json:"-"`
	XXX_sizecache        int32        `json:"-"`
}

func (m *ScmMount) Reset()         { *m = ScmMount{} }
func (m *ScmMount) String() string { return proto.CompactTextString(m) }
func (*ScmMount) ProtoMessage()    {}
func (*ScmMount) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{2}
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

func (m *ScmMount) GetPmem() *PmemDevice {
	if m != nil {
		return m.Pmem
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
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{3}
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
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{4}
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
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{5}
}
func (m *PrepareScmReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PrepareScmReq.Unmarshal(m, b)
}
func (m *PrepareScmReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PrepareScmReq.Marshal(b, m, deterministic)
}
func (dst *PrepareScmReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PrepareScmReq.Merge(dst, src)
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
	Pmems                []*PmemDevice  `protobuf:"bytes,1,rep,name=pmems,proto3" json:"pmems,omitempty"`
	State                *ResponseState `protobuf:"bytes,2,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *PrepareScmResp) Reset()         { *m = PrepareScmResp{} }
func (m *PrepareScmResp) String() string { return proto.CompactTextString(m) }
func (*PrepareScmResp) ProtoMessage()    {}
func (*PrepareScmResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{6}
}
func (m *PrepareScmResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_PrepareScmResp.Unmarshal(m, b)
}
func (m *PrepareScmResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_PrepareScmResp.Marshal(b, m, deterministic)
}
func (dst *PrepareScmResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_PrepareScmResp.Merge(dst, src)
}
func (m *PrepareScmResp) XXX_Size() int {
	return xxx_messageInfo_PrepareScmResp.Size(m)
}
func (m *PrepareScmResp) XXX_DiscardUnknown() {
	xxx_messageInfo_PrepareScmResp.DiscardUnknown(m)
}

var xxx_messageInfo_PrepareScmResp proto.InternalMessageInfo

func (m *PrepareScmResp) GetPmems() []*PmemDevice {
	if m != nil {
		return m.Pmems
	}
	return nil
}

func (m *PrepareScmResp) GetState() *ResponseState {
	if m != nil {
		return m.State
	}
	return nil
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
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{7}
}
func (m *ScanScmReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScanScmReq.Unmarshal(m, b)
}
func (m *ScanScmReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScanScmReq.Marshal(b, m, deterministic)
}
func (dst *ScanScmReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScanScmReq.Merge(dst, src)
}
func (m *ScanScmReq) XXX_Size() int {
	return xxx_messageInfo_ScanScmReq.Size(m)
}
func (m *ScanScmReq) XXX_DiscardUnknown() {
	xxx_messageInfo_ScanScmReq.DiscardUnknown(m)
}

var xxx_messageInfo_ScanScmReq proto.InternalMessageInfo

type ScanScmResp struct {
	Modules              []*ScmModule   `protobuf:"bytes,1,rep,name=modules,proto3" json:"modules,omitempty"`
	State                *ResponseState `protobuf:"bytes,2,opt,name=state,proto3" json:"state,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *ScanScmResp) Reset()         { *m = ScanScmResp{} }
func (m *ScanScmResp) String() string { return proto.CompactTextString(m) }
func (*ScanScmResp) ProtoMessage()    {}
func (*ScanScmResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{8}
}
func (m *ScanScmResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScanScmResp.Unmarshal(m, b)
}
func (m *ScanScmResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScanScmResp.Marshal(b, m, deterministic)
}
func (dst *ScanScmResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScanScmResp.Merge(dst, src)
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
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{9}
}
func (m *FormatScmReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FormatScmReq.Unmarshal(m, b)
}
func (m *FormatScmReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FormatScmReq.Marshal(b, m, deterministic)
}
func (dst *FormatScmReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FormatScmReq.Merge(dst, src)
}
func (m *FormatScmReq) XXX_Size() int {
	return xxx_messageInfo_FormatScmReq.Size(m)
}
func (m *FormatScmReq) XXX_DiscardUnknown() {
	xxx_messageInfo_FormatScmReq.DiscardUnknown(m)
}

var xxx_messageInfo_FormatScmReq proto.InternalMessageInfo

type UpdateScmReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *UpdateScmReq) Reset()         { *m = UpdateScmReq{} }
func (m *UpdateScmReq) String() string { return proto.CompactTextString(m) }
func (*UpdateScmReq) ProtoMessage()    {}
func (*UpdateScmReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{10}
}
func (m *UpdateScmReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_UpdateScmReq.Unmarshal(m, b)
}
func (m *UpdateScmReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_UpdateScmReq.Marshal(b, m, deterministic)
}
func (dst *UpdateScmReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_UpdateScmReq.Merge(dst, src)
}
func (m *UpdateScmReq) XXX_Size() int {
	return xxx_messageInfo_UpdateScmReq.Size(m)
}
func (m *UpdateScmReq) XXX_DiscardUnknown() {
	xxx_messageInfo_UpdateScmReq.DiscardUnknown(m)
}

var xxx_messageInfo_UpdateScmReq proto.InternalMessageInfo

type BurninScmReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *BurninScmReq) Reset()         { *m = BurninScmReq{} }
func (m *BurninScmReq) String() string { return proto.CompactTextString(m) }
func (*BurninScmReq) ProtoMessage()    {}
func (*BurninScmReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_scm_0dd246ee97823ab9, []int{11}
}
func (m *BurninScmReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_BurninScmReq.Unmarshal(m, b)
}
func (m *BurninScmReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_BurninScmReq.Marshal(b, m, deterministic)
}
func (dst *BurninScmReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_BurninScmReq.Merge(dst, src)
}
func (m *BurninScmReq) XXX_Size() int {
	return xxx_messageInfo_BurninScmReq.Size(m)
}
func (m *BurninScmReq) XXX_DiscardUnknown() {
	xxx_messageInfo_BurninScmReq.DiscardUnknown(m)
}

var xxx_messageInfo_BurninScmReq proto.InternalMessageInfo

func init() {
	proto.RegisterType((*ScmModule)(nil), "mgmt.ScmModule")
	proto.RegisterType((*ScmModule_Location)(nil), "mgmt.ScmModule.Location")
	proto.RegisterType((*PmemDevice)(nil), "mgmt.PmemDevice")
	proto.RegisterType((*ScmMount)(nil), "mgmt.ScmMount")
	proto.RegisterType((*ScmModuleResult)(nil), "mgmt.ScmModuleResult")
	proto.RegisterType((*ScmMountResult)(nil), "mgmt.ScmMountResult")
	proto.RegisterType((*PrepareScmReq)(nil), "mgmt.PrepareScmReq")
	proto.RegisterType((*PrepareScmResp)(nil), "mgmt.PrepareScmResp")
	proto.RegisterType((*ScanScmReq)(nil), "mgmt.ScanScmReq")
	proto.RegisterType((*ScanScmResp)(nil), "mgmt.ScanScmResp")
	proto.RegisterType((*FormatScmReq)(nil), "mgmt.FormatScmReq")
	proto.RegisterType((*UpdateScmReq)(nil), "mgmt.UpdateScmReq")
	proto.RegisterType((*BurninScmReq)(nil), "mgmt.BurninScmReq")
}

func init() { proto.RegisterFile("storage_scm.proto", fileDescriptor_storage_scm_0dd246ee97823ab9) }

var fileDescriptor_storage_scm_0dd246ee97823ab9 = []byte{
	// 461 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x94, 0x53, 0xcd, 0x6e, 0xd3, 0x40,
	0x10, 0x96, 0xb1, 0xd3, 0x26, 0x93, 0x9f, 0x96, 0x05, 0x21, 0x2b, 0x07, 0x14, 0x59, 0x80, 0x52,
	0x0e, 0x39, 0x84, 0x37, 0x40, 0x88, 0x13, 0x48, 0xd5, 0x46, 0x88, 0x23, 0xda, 0x8e, 0x47, 0x8d,
	0xa9, 0xf7, 0x07, 0xef, 0xba, 0xd0, 0x87, 0xe6, 0x1d, 0xd0, 0xae, 0xd7, 0x4e, 0x04, 0x12, 0x6d,
	0x6f, 0xfb, 0xcd, 0x8c, 0xe7, 0xfb, 0x19, 0x19, 0x9e, 0x5a, 0xa7, 0x1b, 0x71, 0x4d, 0xdf, 0x2c,
	0xca, 0x8d, 0x69, 0xb4, 0xd3, 0x2c, 0x93, 0xd7, 0xd2, 0x2d, 0x67, 0xa8, 0xa5, 0xd4, 0xaa, 0xab,
	0x15, 0xbf, 0x13, 0x98, 0xec, 0x50, 0x7e, 0xd6, 0x65, 0x5b, 0x13, 0x7b, 0x09, 0x60, 0xf6, 0x77,
	0xb6, 0x42, 0x51, 0x57, 0x65, 0x9e, 0xac, 0x92, 0xf5, 0x9c, 0x1f, 0x55, 0xd8, 0x12, 0xc6, 0x28,
	0x8c, 0xc0, 0xca, 0xdd, 0xe5, 0x4f, 0x56, 0xc9, 0x3a, 0xe3, 0x03, 0x66, 0x6f, 0x21, 0xad, 0x35,
	0xe6, 0xe9, 0x2a, 0x59, 0x4f, 0xb7, 0xf9, 0xc6, 0x73, 0x6d, 0x86, 0xcd, 0x9b, 0x4f, 0x1a, 0x85,
	0xab, 0xb4, 0xe2, 0x7e, 0x68, 0xf9, 0x0b, 0xc6, 0x7d, 0x81, 0xe5, 0x70, 0x8a, 0x7b, 0xa1, 0x14,
	0xd5, 0x91, 0xb0, 0x87, 0x5e, 0x4d, 0x7c, 0x1a, 0x6d, 0x03, 0xdf, 0x9c, 0x1f, 0x55, 0xbc, 0x1a,
	0x49, 0x12, 0x5d, 0x53, 0x37, 0x81, 0x76, 0xce, 0x07, 0xcc, 0x5e, 0xc0, 0x89, 0xd5, 0x78, 0x43,
	0x2e, 0xcf, 0x42, 0x27, 0xa2, 0xe2, 0x3b, 0xc0, 0xa5, 0x24, 0xf9, 0x81, 0x6e, 0x2b, 0x24, 0xc6,
	0x20, 0x6b, 0xdb, 0xe8, 0x74, 0xc2, 0xc3, 0xdb, 0x6f, 0xbd, 0xaa, 0x35, 0xde, 0x94, 0x74, 0x1b,
	0x38, 0x27, 0x7c, 0xc0, 0xec, 0x1c, 0x52, 0x5f, 0x4e, 0x43, 0xd9, 0x3f, 0xfd, 0xb4, 0x6a, 0xa5,
	0x50, 0xba, 0xa4, 0xc8, 0x34, 0xe0, 0xe2, 0x27, 0x8c, 0x43, 0x00, 0xad, 0x72, 0x41, 0xab, 0x72,
	0x46, 0x57, 0xca, 0x45, 0xb6, 0x01, 0xb3, 0x0b, 0x38, 0x95, 0x21, 0x25, 0x6f, 0x32, 0x5d, 0x4f,
	0xb7, 0x67, 0x7f, 0xa5, 0xc7, 0xfb, 0x3e, 0x7b, 0x05, 0x99, 0x91, 0x24, 0x63, 0xca, 0xe7, 0xdd,
	0xdc, 0xc1, 0x10, 0x0f, 0xdd, 0x62, 0x0f, 0x67, 0x87, 0x6f, 0xc9, 0xb6, 0xb5, 0xeb, 0xaf, 0x93,
	0x3c, 0xe0, 0x3a, 0xec, 0x02, 0x46, 0xd6, 0x09, 0x47, 0xc1, 0xfe, 0x74, 0xfb, 0xac, 0x9b, 0xe6,
	0x64, 0x8d, 0x56, 0x96, 0x76, 0xbe, 0xc5, 0xbb, 0x89, 0xe2, 0x2b, 0x2c, 0x7a, 0x8b, 0x91, 0xe8,
	0xff, 0x46, 0x1f, 0xbc, 0xf8, 0x35, 0xcc, 0x2f, 0x1b, 0x32, 0xa2, 0xa1, 0x1d, 0x4a, 0x4e, 0x3f,
	0xd8, 0x73, 0x18, 0x35, 0x64, 0xa9, 0x5b, 0x3a, 0xe6, 0x1d, 0x28, 0x10, 0x16, 0xc7, 0x63, 0xd6,
	0xb0, 0x37, 0x30, 0xf2, 0x19, 0xd8, 0x3c, 0x09, 0x51, 0xfe, 0x1b, 0x51, 0xd7, 0x7e, 0x8c, 0x96,
	0x19, 0xc0, 0x0e, 0x85, 0xea, 0x84, 0x14, 0x08, 0xd3, 0x01, 0x59, 0x73, 0x7c, 0xbc, 0xe4, 0x9e,
	0xe3, 0x3d, 0x82, 0x72, 0x01, 0xb3, 0x8f, 0xba, 0x91, 0xc2, 0x45, 0xd2, 0x05, 0xcc, 0xbe, 0x98,
	0x52, 0x38, 0x3a, 0xe0, 0xf7, 0x6d, 0xa3, 0xaa, 0x28, 0xea, 0xea, 0x24, 0xfc, 0xcd, 0xef, 0xfe,
	0x04, 0x00, 0x00, 0xff, 0xff, 0x4f, 0x8f, 0xe8, 0xec, 0xf6, 0x03, 0x00, 0x00,
}
