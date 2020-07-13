// Code generated by protoc-gen-go. DO NOT EDIT.
// source: firmware.proto

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

type FirmwareUpdateReq_DeviceType int32

const (
	FirmwareUpdateReq_SCM  FirmwareUpdateReq_DeviceType = 0
	FirmwareUpdateReq_NVMe FirmwareUpdateReq_DeviceType = 1
)

var FirmwareUpdateReq_DeviceType_name = map[int32]string{
	0: "SCM",
	1: "NVMe",
}

var FirmwareUpdateReq_DeviceType_value = map[string]int32{
	"SCM":  0,
	"NVMe": 1,
}

func (x FirmwareUpdateReq_DeviceType) String() string {
	return proto.EnumName(FirmwareUpdateReq_DeviceType_name, int32(x))
}

func (FirmwareUpdateReq_DeviceType) EnumDescriptor() ([]byte, []int) {
	return fileDescriptor_138455e383c002dd, []int{3, 0}
}

type FirmwareQueryReq struct {
	QueryScm             bool     `protobuf:"varint,1,opt,name=queryScm,proto3" json:"queryScm,omitempty"`
	QueryNvme            bool     `protobuf:"varint,2,opt,name=queryNvme,proto3" json:"queryNvme,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *FirmwareQueryReq) Reset()         { *m = FirmwareQueryReq{} }
func (m *FirmwareQueryReq) String() string { return proto.CompactTextString(m) }
func (*FirmwareQueryReq) ProtoMessage()    {}
func (*FirmwareQueryReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_138455e383c002dd, []int{0}
}

func (m *FirmwareQueryReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FirmwareQueryReq.Unmarshal(m, b)
}
func (m *FirmwareQueryReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FirmwareQueryReq.Marshal(b, m, deterministic)
}
func (m *FirmwareQueryReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FirmwareQueryReq.Merge(m, src)
}
func (m *FirmwareQueryReq) XXX_Size() int {
	return xxx_messageInfo_FirmwareQueryReq.Size(m)
}
func (m *FirmwareQueryReq) XXX_DiscardUnknown() {
	xxx_messageInfo_FirmwareQueryReq.DiscardUnknown(m)
}

var xxx_messageInfo_FirmwareQueryReq proto.InternalMessageInfo

func (m *FirmwareQueryReq) GetQueryScm() bool {
	if m != nil {
		return m.QueryScm
	}
	return false
}

func (m *FirmwareQueryReq) GetQueryNvme() bool {
	if m != nil {
		return m.QueryNvme
	}
	return false
}

type ScmFirmwareQueryResp struct {
	Module               *ScmModule `protobuf:"bytes,1,opt,name=module,proto3" json:"module,omitempty"`
	ActiveVersion        string     `protobuf:"bytes,2,opt,name=activeVersion,proto3" json:"activeVersion,omitempty"`
	StagedVersion        string     `protobuf:"bytes,3,opt,name=stagedVersion,proto3" json:"stagedVersion,omitempty"`
	ImageMaxSizeBytes    uint32     `protobuf:"varint,4,opt,name=imageMaxSizeBytes,proto3" json:"imageMaxSizeBytes,omitempty"`
	UpdateStatus         uint32     `protobuf:"varint,5,opt,name=updateStatus,proto3" json:"updateStatus,omitempty"`
	Error                string     `protobuf:"bytes,6,opt,name=error,proto3" json:"error,omitempty"`
	XXX_NoUnkeyedLiteral struct{}   `json:"-"`
	XXX_unrecognized     []byte     `json:"-"`
	XXX_sizecache        int32      `json:"-"`
}

func (m *ScmFirmwareQueryResp) Reset()         { *m = ScmFirmwareQueryResp{} }
func (m *ScmFirmwareQueryResp) String() string { return proto.CompactTextString(m) }
func (*ScmFirmwareQueryResp) ProtoMessage()    {}
func (*ScmFirmwareQueryResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_138455e383c002dd, []int{1}
}

func (m *ScmFirmwareQueryResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmFirmwareQueryResp.Unmarshal(m, b)
}
func (m *ScmFirmwareQueryResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmFirmwareQueryResp.Marshal(b, m, deterministic)
}
func (m *ScmFirmwareQueryResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmFirmwareQueryResp.Merge(m, src)
}
func (m *ScmFirmwareQueryResp) XXX_Size() int {
	return xxx_messageInfo_ScmFirmwareQueryResp.Size(m)
}
func (m *ScmFirmwareQueryResp) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmFirmwareQueryResp.DiscardUnknown(m)
}

var xxx_messageInfo_ScmFirmwareQueryResp proto.InternalMessageInfo

func (m *ScmFirmwareQueryResp) GetModule() *ScmModule {
	if m != nil {
		return m.Module
	}
	return nil
}

func (m *ScmFirmwareQueryResp) GetActiveVersion() string {
	if m != nil {
		return m.ActiveVersion
	}
	return ""
}

func (m *ScmFirmwareQueryResp) GetStagedVersion() string {
	if m != nil {
		return m.StagedVersion
	}
	return ""
}

func (m *ScmFirmwareQueryResp) GetImageMaxSizeBytes() uint32 {
	if m != nil {
		return m.ImageMaxSizeBytes
	}
	return 0
}

func (m *ScmFirmwareQueryResp) GetUpdateStatus() uint32 {
	if m != nil {
		return m.UpdateStatus
	}
	return 0
}

func (m *ScmFirmwareQueryResp) GetError() string {
	if m != nil {
		return m.Error
	}
	return ""
}

type FirmwareQueryResp struct {
	ScmResults           []*ScmFirmwareQueryResp `protobuf:"bytes,1,rep,name=scmResults,proto3" json:"scmResults,omitempty"`
	XXX_NoUnkeyedLiteral struct{}                `json:"-"`
	XXX_unrecognized     []byte                  `json:"-"`
	XXX_sizecache        int32                   `json:"-"`
}

func (m *FirmwareQueryResp) Reset()         { *m = FirmwareQueryResp{} }
func (m *FirmwareQueryResp) String() string { return proto.CompactTextString(m) }
func (*FirmwareQueryResp) ProtoMessage()    {}
func (*FirmwareQueryResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_138455e383c002dd, []int{2}
}

func (m *FirmwareQueryResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FirmwareQueryResp.Unmarshal(m, b)
}
func (m *FirmwareQueryResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FirmwareQueryResp.Marshal(b, m, deterministic)
}
func (m *FirmwareQueryResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FirmwareQueryResp.Merge(m, src)
}
func (m *FirmwareQueryResp) XXX_Size() int {
	return xxx_messageInfo_FirmwareQueryResp.Size(m)
}
func (m *FirmwareQueryResp) XXX_DiscardUnknown() {
	xxx_messageInfo_FirmwareQueryResp.DiscardUnknown(m)
}

var xxx_messageInfo_FirmwareQueryResp proto.InternalMessageInfo

func (m *FirmwareQueryResp) GetScmResults() []*ScmFirmwareQueryResp {
	if m != nil {
		return m.ScmResults
	}
	return nil
}

type FirmwareUpdateReq struct {
	FirmwarePath         string                       `protobuf:"bytes,1,opt,name=firmwarePath,proto3" json:"firmwarePath,omitempty"`
	Type                 FirmwareUpdateReq_DeviceType `protobuf:"varint,2,opt,name=type,proto3,enum=ctl.FirmwareUpdateReq_DeviceType" json:"type,omitempty"`
	XXX_NoUnkeyedLiteral struct{}                     `json:"-"`
	XXX_unrecognized     []byte                       `json:"-"`
	XXX_sizecache        int32                        `json:"-"`
}

func (m *FirmwareUpdateReq) Reset()         { *m = FirmwareUpdateReq{} }
func (m *FirmwareUpdateReq) String() string { return proto.CompactTextString(m) }
func (*FirmwareUpdateReq) ProtoMessage()    {}
func (*FirmwareUpdateReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_138455e383c002dd, []int{3}
}

func (m *FirmwareUpdateReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FirmwareUpdateReq.Unmarshal(m, b)
}
func (m *FirmwareUpdateReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FirmwareUpdateReq.Marshal(b, m, deterministic)
}
func (m *FirmwareUpdateReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FirmwareUpdateReq.Merge(m, src)
}
func (m *FirmwareUpdateReq) XXX_Size() int {
	return xxx_messageInfo_FirmwareUpdateReq.Size(m)
}
func (m *FirmwareUpdateReq) XXX_DiscardUnknown() {
	xxx_messageInfo_FirmwareUpdateReq.DiscardUnknown(m)
}

var xxx_messageInfo_FirmwareUpdateReq proto.InternalMessageInfo

func (m *FirmwareUpdateReq) GetFirmwarePath() string {
	if m != nil {
		return m.FirmwarePath
	}
	return ""
}

func (m *FirmwareUpdateReq) GetType() FirmwareUpdateReq_DeviceType {
	if m != nil {
		return m.Type
	}
	return FirmwareUpdateReq_SCM
}

type ScmFirmwareUpdateResp struct {
	Module               *ScmModule `protobuf:"bytes,1,opt,name=module,proto3" json:"module,omitempty"`
	Error                string     `protobuf:"bytes,2,opt,name=error,proto3" json:"error,omitempty"`
	XXX_NoUnkeyedLiteral struct{}   `json:"-"`
	XXX_unrecognized     []byte     `json:"-"`
	XXX_sizecache        int32      `json:"-"`
}

func (m *ScmFirmwareUpdateResp) Reset()         { *m = ScmFirmwareUpdateResp{} }
func (m *ScmFirmwareUpdateResp) String() string { return proto.CompactTextString(m) }
func (*ScmFirmwareUpdateResp) ProtoMessage()    {}
func (*ScmFirmwareUpdateResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_138455e383c002dd, []int{4}
}

func (m *ScmFirmwareUpdateResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ScmFirmwareUpdateResp.Unmarshal(m, b)
}
func (m *ScmFirmwareUpdateResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ScmFirmwareUpdateResp.Marshal(b, m, deterministic)
}
func (m *ScmFirmwareUpdateResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ScmFirmwareUpdateResp.Merge(m, src)
}
func (m *ScmFirmwareUpdateResp) XXX_Size() int {
	return xxx_messageInfo_ScmFirmwareUpdateResp.Size(m)
}
func (m *ScmFirmwareUpdateResp) XXX_DiscardUnknown() {
	xxx_messageInfo_ScmFirmwareUpdateResp.DiscardUnknown(m)
}

var xxx_messageInfo_ScmFirmwareUpdateResp proto.InternalMessageInfo

func (m *ScmFirmwareUpdateResp) GetModule() *ScmModule {
	if m != nil {
		return m.Module
	}
	return nil
}

func (m *ScmFirmwareUpdateResp) GetError() string {
	if m != nil {
		return m.Error
	}
	return ""
}

type FirmwareUpdateResp struct {
	ScmResults           []*ScmFirmwareUpdateResp `protobuf:"bytes,1,rep,name=scmResults,proto3" json:"scmResults,omitempty"`
	XXX_NoUnkeyedLiteral struct{}                 `json:"-"`
	XXX_unrecognized     []byte                   `json:"-"`
	XXX_sizecache        int32                    `json:"-"`
}

func (m *FirmwareUpdateResp) Reset()         { *m = FirmwareUpdateResp{} }
func (m *FirmwareUpdateResp) String() string { return proto.CompactTextString(m) }
func (*FirmwareUpdateResp) ProtoMessage()    {}
func (*FirmwareUpdateResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_138455e383c002dd, []int{5}
}

func (m *FirmwareUpdateResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FirmwareUpdateResp.Unmarshal(m, b)
}
func (m *FirmwareUpdateResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FirmwareUpdateResp.Marshal(b, m, deterministic)
}
func (m *FirmwareUpdateResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FirmwareUpdateResp.Merge(m, src)
}
func (m *FirmwareUpdateResp) XXX_Size() int {
	return xxx_messageInfo_FirmwareUpdateResp.Size(m)
}
func (m *FirmwareUpdateResp) XXX_DiscardUnknown() {
	xxx_messageInfo_FirmwareUpdateResp.DiscardUnknown(m)
}

var xxx_messageInfo_FirmwareUpdateResp proto.InternalMessageInfo

func (m *FirmwareUpdateResp) GetScmResults() []*ScmFirmwareUpdateResp {
	if m != nil {
		return m.ScmResults
	}
	return nil
}

func init() {
	proto.RegisterEnum("ctl.FirmwareUpdateReq_DeviceType", FirmwareUpdateReq_DeviceType_name, FirmwareUpdateReq_DeviceType_value)
	proto.RegisterType((*FirmwareQueryReq)(nil), "ctl.FirmwareQueryReq")
	proto.RegisterType((*ScmFirmwareQueryResp)(nil), "ctl.ScmFirmwareQueryResp")
	proto.RegisterType((*FirmwareQueryResp)(nil), "ctl.FirmwareQueryResp")
	proto.RegisterType((*FirmwareUpdateReq)(nil), "ctl.FirmwareUpdateReq")
	proto.RegisterType((*ScmFirmwareUpdateResp)(nil), "ctl.ScmFirmwareUpdateResp")
	proto.RegisterType((*FirmwareUpdateResp)(nil), "ctl.FirmwareUpdateResp")
}

func init() {
	proto.RegisterFile("firmware.proto", fileDescriptor_138455e383c002dd)
}

var fileDescriptor_138455e383c002dd = []byte{
	// 389 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x94, 0x92, 0x41, 0xaf, 0xd2, 0x40,
	0x14, 0x85, 0xed, 0x2b, 0x0f, 0xe1, 0xbe, 0xf7, 0x08, 0x4c, 0x30, 0xa9, 0xc4, 0x44, 0x9c, 0x18,
	0xc3, 0xc2, 0x74, 0x81, 0x71, 0xa1, 0x4b, 0x35, 0xae, 0x2c, 0xc1, 0xa9, 0xb0, 0x35, 0xe3, 0x70,
	0xc5, 0x26, 0x1d, 0x5b, 0x66, 0xa6, 0x68, 0xfd, 0x13, 0xfe, 0x57, 0x7f, 0x81, 0x61, 0x4a, 0x69,
	0x4b, 0xd9, 0xbc, 0xdd, 0xcc, 0xb9, 0x5f, 0xce, 0xed, 0x9c, 0x53, 0x18, 0x7c, 0x8f, 0x94, 0xfc,
	0xc5, 0x15, 0xfa, 0xa9, 0x4a, 0x4c, 0x42, 0x5c, 0x61, 0xe2, 0xc9, 0x48, 0x9b, 0x44, 0xf1, 0x2d,
	0x7e, 0xd5, 0x42, 0x16, 0x3a, 0xfd, 0x04, 0xc3, 0x8f, 0x47, 0xf2, 0x73, 0x86, 0x2a, 0x67, 0xb8,
	0x23, 0x13, 0xe8, 0xed, 0x0e, 0xe7, 0x50, 0x48, 0xcf, 0x99, 0x3a, 0xb3, 0x1e, 0x3b, 0xdd, 0xc9,
	0x13, 0xe8, 0xdb, 0xf3, 0x62, 0x2f, 0xd1, 0xbb, 0xb2, 0xc3, 0x4a, 0xa0, 0xff, 0x1c, 0x18, 0x87,
	0x42, 0x9e, 0x39, 0xea, 0x94, 0xbc, 0x80, 0xae, 0x4c, 0x36, 0x59, 0x8c, 0xd6, 0xf0, 0x66, 0x3e,
	0xf0, 0x85, 0x89, 0xfd, 0x50, 0xc8, 0xc0, 0xaa, 0xec, 0x38, 0x25, 0xcf, 0xe1, 0x8e, 0x0b, 0x13,
	0xed, 0x71, 0x8d, 0x4a, 0x47, 0xc9, 0x4f, 0xbb, 0xa2, 0xcf, 0x9a, 0xe2, 0x81, 0xd2, 0x86, 0x6f,
	0x71, 0x53, 0x52, 0x6e, 0x41, 0x35, 0x44, 0xf2, 0x12, 0x46, 0x91, 0xe4, 0x5b, 0x0c, 0xf8, 0xef,
	0x30, 0xfa, 0x83, 0xef, 0x72, 0x83, 0xda, 0xeb, 0x4c, 0x9d, 0xd9, 0x1d, 0x6b, 0x0f, 0x08, 0x85,
	0xdb, 0x2c, 0xdd, 0x70, 0x83, 0xa1, 0xe1, 0x26, 0xd3, 0xde, 0xb5, 0x05, 0x1b, 0x1a, 0x19, 0xc3,
	0x35, 0x2a, 0x95, 0x28, 0xaf, 0x6b, 0xf7, 0x15, 0x17, 0xba, 0x80, 0x51, 0xfb, 0xc1, 0x6f, 0x00,
	0xb4, 0x90, 0x0c, 0x75, 0x16, 0x1b, 0xed, 0x39, 0x53, 0x77, 0x76, 0x33, 0x7f, 0x5c, 0x3e, 0xba,
	0x85, 0xb3, 0x1a, 0x4c, 0xff, 0x3a, 0x95, 0xe1, 0xca, 0xae, 0x3f, 0x94, 0x42, 0xe1, 0xb6, 0xac,
	0x74, 0xc9, 0xcd, 0x0f, 0x9b, 0x63, 0x9f, 0x35, 0x34, 0xf2, 0x1a, 0x3a, 0x26, 0x4f, 0x8b, 0x5e,
	0x06, 0xf3, 0x67, 0x76, 0x5d, 0xcb, 0xc9, 0xff, 0x80, 0xfb, 0x48, 0xe0, 0x97, 0x3c, 0x45, 0x66,
	0x71, 0xfa, 0x14, 0xa0, 0xd2, 0xc8, 0x43, 0x70, 0xc3, 0xf7, 0xc1, 0xf0, 0x01, 0xe9, 0x41, 0x67,
	0xb1, 0x0e, 0x70, 0xe8, 0xd0, 0x15, 0x3c, 0xaa, 0x7d, 0x75, 0xe9, 0x74, 0x8f, 0x5a, 0x4f, 0xc1,
	0x5d, 0xd5, 0x83, 0x5b, 0x02, 0xb9, 0xe0, 0xf9, 0xf6, 0x42, 0x72, 0x93, 0xf3, 0xe4, 0x2a, 0xbe,
	0x1e, 0xdd, 0xb7, 0xae, 0xfd, 0xa9, 0x5f, 0xfd, 0x0f, 0x00, 0x00, 0xff, 0xff, 0x05, 0xbf, 0xa0,
	0xd4, 0xfe, 0x02, 0x00, 0x00,
}
