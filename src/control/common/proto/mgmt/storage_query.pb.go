// Code generated by protoc-gen-go. DO NOT EDIT.
// source: storage_query.proto

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

type BioHealthReq struct {
	DevUuid              string   `protobuf:"bytes,1,opt,name=dev_uuid,json=devUuid,proto3" json:"dev_uuid,omitempty"`
	TgtId                string   `protobuf:"bytes,2,opt,name=tgt_id,json=tgtId,proto3" json:"tgt_id,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *BioHealthReq) Reset()         { *m = BioHealthReq{} }
func (m *BioHealthReq) String() string { return proto.CompactTextString(m) }
func (*BioHealthReq) ProtoMessage()    {}
func (*BioHealthReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_query_01fad71e3cc69a58, []int{0}
}
func (m *BioHealthReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_BioHealthReq.Unmarshal(m, b)
}
func (m *BioHealthReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_BioHealthReq.Marshal(b, m, deterministic)
}
func (dst *BioHealthReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_BioHealthReq.Merge(dst, src)
}
func (m *BioHealthReq) XXX_Size() int {
	return xxx_messageInfo_BioHealthReq.Size(m)
}
func (m *BioHealthReq) XXX_DiscardUnknown() {
	xxx_messageInfo_BioHealthReq.DiscardUnknown(m)
}

var xxx_messageInfo_BioHealthReq proto.InternalMessageInfo

func (m *BioHealthReq) GetDevUuid() string {
	if m != nil {
		return m.DevUuid
	}
	return ""
}

func (m *BioHealthReq) GetTgtId() string {
	if m != nil {
		return m.TgtId
	}
	return ""
}

type BioHealthResp struct {
	Status               int32    `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	DevUuid              string   `protobuf:"bytes,2,opt,name=dev_uuid,json=devUuid,proto3" json:"dev_uuid,omitempty"`
	ErrorCount           uint64   `protobuf:"varint,3,opt,name=error_count,json=errorCount,proto3" json:"error_count,omitempty"`
	Temperature          uint32   `protobuf:"varint,4,opt,name=temperature,proto3" json:"temperature,omitempty"`
	MediaErrors          uint64   `protobuf:"varint,5,opt,name=media_errors,json=mediaErrors,proto3" json:"media_errors,omitempty"`
	ReadErrs             uint32   `protobuf:"varint,6,opt,name=read_errs,json=readErrs,proto3" json:"read_errs,omitempty"`
	WriteErrs            uint32   `protobuf:"varint,7,opt,name=write_errs,json=writeErrs,proto3" json:"write_errs,omitempty"`
	UnmapErrs            uint32   `protobuf:"varint,8,opt,name=unmap_errs,json=unmapErrs,proto3" json:"unmap_errs,omitempty"`
	ChecksumErrs         uint32   `protobuf:"varint,9,opt,name=checksum_errs,json=checksumErrs,proto3" json:"checksum_errs,omitempty"`
	Temp                 bool     `protobuf:"varint,10,opt,name=temp,proto3" json:"temp,omitempty"`
	Spare                bool     `protobuf:"varint,11,opt,name=spare,proto3" json:"spare,omitempty"`
	Readonly             bool     `protobuf:"varint,12,opt,name=readonly,proto3" json:"readonly,omitempty"`
	DeviceReliability    bool     `protobuf:"varint,13,opt,name=device_reliability,json=deviceReliability,proto3" json:"device_reliability,omitempty"`
	VolatileMemory       bool     `protobuf:"varint,14,opt,name=volatile_memory,json=volatileMemory,proto3" json:"volatile_memory,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *BioHealthResp) Reset()         { *m = BioHealthResp{} }
func (m *BioHealthResp) String() string { return proto.CompactTextString(m) }
func (*BioHealthResp) ProtoMessage()    {}
func (*BioHealthResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_query_01fad71e3cc69a58, []int{1}
}
func (m *BioHealthResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_BioHealthResp.Unmarshal(m, b)
}
func (m *BioHealthResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_BioHealthResp.Marshal(b, m, deterministic)
}
func (dst *BioHealthResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_BioHealthResp.Merge(dst, src)
}
func (m *BioHealthResp) XXX_Size() int {
	return xxx_messageInfo_BioHealthResp.Size(m)
}
func (m *BioHealthResp) XXX_DiscardUnknown() {
	xxx_messageInfo_BioHealthResp.DiscardUnknown(m)
}

var xxx_messageInfo_BioHealthResp proto.InternalMessageInfo

func (m *BioHealthResp) GetStatus() int32 {
	if m != nil {
		return m.Status
	}
	return 0
}

func (m *BioHealthResp) GetDevUuid() string {
	if m != nil {
		return m.DevUuid
	}
	return ""
}

func (m *BioHealthResp) GetErrorCount() uint64 {
	if m != nil {
		return m.ErrorCount
	}
	return 0
}

func (m *BioHealthResp) GetTemperature() uint32 {
	if m != nil {
		return m.Temperature
	}
	return 0
}

func (m *BioHealthResp) GetMediaErrors() uint64 {
	if m != nil {
		return m.MediaErrors
	}
	return 0
}

func (m *BioHealthResp) GetReadErrs() uint32 {
	if m != nil {
		return m.ReadErrs
	}
	return 0
}

func (m *BioHealthResp) GetWriteErrs() uint32 {
	if m != nil {
		return m.WriteErrs
	}
	return 0
}

func (m *BioHealthResp) GetUnmapErrs() uint32 {
	if m != nil {
		return m.UnmapErrs
	}
	return 0
}

func (m *BioHealthResp) GetChecksumErrs() uint32 {
	if m != nil {
		return m.ChecksumErrs
	}
	return 0
}

func (m *BioHealthResp) GetTemp() bool {
	if m != nil {
		return m.Temp
	}
	return false
}

func (m *BioHealthResp) GetSpare() bool {
	if m != nil {
		return m.Spare
	}
	return false
}

func (m *BioHealthResp) GetReadonly() bool {
	if m != nil {
		return m.Readonly
	}
	return false
}

func (m *BioHealthResp) GetDeviceReliability() bool {
	if m != nil {
		return m.DeviceReliability
	}
	return false
}

func (m *BioHealthResp) GetVolatileMemory() bool {
	if m != nil {
		return m.VolatileMemory
	}
	return false
}

type SmdDevReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SmdDevReq) Reset()         { *m = SmdDevReq{} }
func (m *SmdDevReq) String() string { return proto.CompactTextString(m) }
func (*SmdDevReq) ProtoMessage()    {}
func (*SmdDevReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_query_01fad71e3cc69a58, []int{2}
}
func (m *SmdDevReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SmdDevReq.Unmarshal(m, b)
}
func (m *SmdDevReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SmdDevReq.Marshal(b, m, deterministic)
}
func (dst *SmdDevReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SmdDevReq.Merge(dst, src)
}
func (m *SmdDevReq) XXX_Size() int {
	return xxx_messageInfo_SmdDevReq.Size(m)
}
func (m *SmdDevReq) XXX_DiscardUnknown() {
	xxx_messageInfo_SmdDevReq.DiscardUnknown(m)
}

var xxx_messageInfo_SmdDevReq proto.InternalMessageInfo

type SmdDevResp struct {
	Status               int32                `protobuf:"varint,1,opt,name=status,proto3" json:"status,omitempty"`
	Devices              []*SmdDevResp_Device `protobuf:"bytes,2,rep,name=devices,proto3" json:"devices,omitempty"`
	XXX_NoUnkeyedLiteral struct{}             `json:"-"`
	XXX_unrecognized     []byte               `json:"-"`
	XXX_sizecache        int32                `json:"-"`
}

func (m *SmdDevResp) Reset()         { *m = SmdDevResp{} }
func (m *SmdDevResp) String() string { return proto.CompactTextString(m) }
func (*SmdDevResp) ProtoMessage()    {}
func (*SmdDevResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_query_01fad71e3cc69a58, []int{3}
}
func (m *SmdDevResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SmdDevResp.Unmarshal(m, b)
}
func (m *SmdDevResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SmdDevResp.Marshal(b, m, deterministic)
}
func (dst *SmdDevResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SmdDevResp.Merge(dst, src)
}
func (m *SmdDevResp) XXX_Size() int {
	return xxx_messageInfo_SmdDevResp.Size(m)
}
func (m *SmdDevResp) XXX_DiscardUnknown() {
	xxx_messageInfo_SmdDevResp.DiscardUnknown(m)
}

var xxx_messageInfo_SmdDevResp proto.InternalMessageInfo

func (m *SmdDevResp) GetStatus() int32 {
	if m != nil {
		return m.Status
	}
	return 0
}

func (m *SmdDevResp) GetDevices() []*SmdDevResp_Device {
	if m != nil {
		return m.Devices
	}
	return nil
}

type SmdDevResp_Device struct {
	Uuid                 string   `protobuf:"bytes,1,opt,name=uuid,proto3" json:"uuid,omitempty"`
	TgtIds               []int32  `protobuf:"varint,2,rep,packed,name=tgt_ids,json=tgtIds,proto3" json:"tgt_ids,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *SmdDevResp_Device) Reset()         { *m = SmdDevResp_Device{} }
func (m *SmdDevResp_Device) String() string { return proto.CompactTextString(m) }
func (*SmdDevResp_Device) ProtoMessage()    {}
func (*SmdDevResp_Device) Descriptor() ([]byte, []int) {
	return fileDescriptor_storage_query_01fad71e3cc69a58, []int{3, 0}
}
func (m *SmdDevResp_Device) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_SmdDevResp_Device.Unmarshal(m, b)
}
func (m *SmdDevResp_Device) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_SmdDevResp_Device.Marshal(b, m, deterministic)
}
func (dst *SmdDevResp_Device) XXX_Merge(src proto.Message) {
	xxx_messageInfo_SmdDevResp_Device.Merge(dst, src)
}
func (m *SmdDevResp_Device) XXX_Size() int {
	return xxx_messageInfo_SmdDevResp_Device.Size(m)
}
func (m *SmdDevResp_Device) XXX_DiscardUnknown() {
	xxx_messageInfo_SmdDevResp_Device.DiscardUnknown(m)
}

var xxx_messageInfo_SmdDevResp_Device proto.InternalMessageInfo

func (m *SmdDevResp_Device) GetUuid() string {
	if m != nil {
		return m.Uuid
	}
	return ""
}

func (m *SmdDevResp_Device) GetTgtIds() []int32 {
	if m != nil {
		return m.TgtIds
	}
	return nil
}

func init() {
	proto.RegisterType((*BioHealthReq)(nil), "mgmt.BioHealthReq")
	proto.RegisterType((*BioHealthResp)(nil), "mgmt.BioHealthResp")
	proto.RegisterType((*SmdDevReq)(nil), "mgmt.SmdDevReq")
	proto.RegisterType((*SmdDevResp)(nil), "mgmt.SmdDevResp")
	proto.RegisterType((*SmdDevResp_Device)(nil), "mgmt.SmdDevResp.Device")
}

func init() { proto.RegisterFile("storage_query.proto", fileDescriptor_storage_query_01fad71e3cc69a58) }

var fileDescriptor_storage_query_01fad71e3cc69a58 = []byte{
	// 425 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x7c, 0x92, 0xcd, 0x8e, 0xd3, 0x30,
	0x14, 0x85, 0x95, 0xb6, 0x49, 0x9b, 0x9b, 0x76, 0x10, 0xe6, 0x67, 0xcc, 0x20, 0x44, 0x28, 0x0b,
	0xb2, 0xa1, 0x12, 0x20, 0xf6, 0x08, 0x66, 0x24, 0x58, 0xb0, 0x09, 0x62, 0x1d, 0x79, 0xea, 0xab,
	0x8e, 0x45, 0x5c, 0xa7, 0xfe, 0x09, 0xea, 0x4b, 0xf0, 0x58, 0x3c, 0x17, 0xca, 0x35, 0x61, 0x3a,
	0x1b, 0x76, 0xbe, 0xdf, 0x39, 0x3e, 0xb9, 0x8a, 0x0f, 0x3c, 0x70, 0xde, 0x58, 0xb1, 0xc3, 0xe6,
	0x10, 0xd0, 0x1e, 0x37, 0x9d, 0x35, 0xde, 0xb0, 0x99, 0xde, 0x69, 0xbf, 0xfe, 0x00, 0xcb, 0x8f,
	0xca, 0x7c, 0x46, 0xd1, 0xfa, 0x9b, 0x1a, 0x0f, 0xec, 0x09, 0x2c, 0x24, 0xf6, 0x4d, 0x08, 0x4a,
	0xf2, 0xa4, 0x4c, 0xaa, 0xbc, 0x9e, 0x4b, 0xec, 0xbf, 0x07, 0x25, 0xd9, 0x23, 0xc8, 0xfc, 0xce,
	0x37, 0x4a, 0xf2, 0x09, 0x09, 0xa9, 0xdf, 0xf9, 0x2f, 0x72, 0xfd, 0x7b, 0x0a, 0xab, 0x93, 0x08,
	0xd7, 0xb1, 0xc7, 0x90, 0x39, 0x2f, 0x7c, 0x70, 0x94, 0x90, 0xd6, 0x7f, 0xa7, 0x3b, 0xd9, 0x93,
	0xbb, 0xd9, 0xcf, 0xa1, 0x40, 0x6b, 0x8d, 0x6d, 0xb6, 0x26, 0xec, 0x3d, 0x9f, 0x96, 0x49, 0x35,
	0xab, 0x81, 0xd0, 0xa7, 0x81, 0xb0, 0x12, 0x0a, 0x8f, 0xba, 0x43, 0x2b, 0x7c, 0xb0, 0xc8, 0x67,
	0x65, 0x52, 0xad, 0xea, 0x53, 0xc4, 0x5e, 0xc0, 0x52, 0xa3, 0x54, 0xa2, 0xa1, 0x5b, 0x8e, 0xa7,
	0x94, 0x51, 0x10, 0xbb, 0x22, 0xc4, 0x9e, 0x42, 0x6e, 0x51, 0xc8, 0xc1, 0xe1, 0x78, 0x46, 0x11,
	0x8b, 0x01, 0x5c, 0x59, 0xeb, 0xd8, 0x33, 0x80, 0x9f, 0x56, 0x79, 0x8c, 0xea, 0x9c, 0xd4, 0x9c,
	0xc8, 0x28, 0x87, 0xbd, 0x16, 0x5d, 0x94, 0x17, 0x51, 0x26, 0x42, 0xf2, 0x4b, 0x58, 0x6d, 0x6f,
	0x70, 0xfb, 0xc3, 0x05, 0x1d, 0x1d, 0x39, 0x39, 0x96, 0x23, 0x24, 0x13, 0x83, 0xd9, 0xb0, 0x31,
	0x87, 0x32, 0xa9, 0x16, 0x35, 0x9d, 0xd9, 0x43, 0x48, 0x5d, 0x27, 0x2c, 0xf2, 0x82, 0x60, 0x1c,
	0xd8, 0x05, 0xd0, 0x62, 0x66, 0xdf, 0x1e, 0xf9, 0x92, 0x84, 0x7f, 0x33, 0x7b, 0x0d, 0x4c, 0x62,
	0xaf, 0xb6, 0xd8, 0x58, 0x6c, 0x95, 0xb8, 0x56, 0xad, 0xf2, 0x47, 0xbe, 0x22, 0xd7, 0xfd, 0xa8,
	0xd4, 0xb7, 0x02, 0x7b, 0x05, 0xf7, 0x7a, 0xd3, 0x0a, 0xaf, 0x5a, 0x6c, 0x34, 0x6a, 0x63, 0x8f,
	0xfc, 0x8c, 0xbc, 0x67, 0x23, 0xfe, 0x4a, 0x74, 0x5d, 0x40, 0xfe, 0x4d, 0xcb, 0x4b, 0xec, 0x6b,
	0x3c, 0xac, 0x7f, 0x25, 0x00, 0xe3, 0xf4, 0x9f, 0x27, 0x7d, 0x03, 0xf3, 0xf8, 0x45, 0xc7, 0x27,
	0xe5, 0xb4, 0x2a, 0xde, 0x9e, 0x6f, 0x86, 0x5a, 0x6d, 0x6e, 0xaf, 0x6e, 0x2e, 0xe3, 0x46, 0xa3,
	0xef, 0xe2, 0x3d, 0x64, 0x11, 0x0d, 0xbf, 0xe3, 0xa4, 0x67, 0x74, 0x66, 0xe7, 0x30, 0x8f, 0x25,
	0x8b, 0x81, 0x69, 0x9d, 0x51, 0xcb, 0xdc, 0x75, 0x46, 0xad, 0x7d, 0xf7, 0x27, 0x00, 0x00, 0xff,
	0xff, 0x9a, 0xcb, 0x31, 0xe2, 0xcc, 0x02, 0x00, 0x00,
}
