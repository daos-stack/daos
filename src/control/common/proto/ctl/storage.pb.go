// Code generated by protoc-gen-go. DO NOT EDIT.
// source: ctl/storage.proto

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

type StoragePrepareReq struct {
	Nvme                 *PrepareNvmeReq `protobuf:"bytes,1,opt,name=nvme,proto3" json:"nvme,omitempty"`
	Scm                  *PrepareScmReq  `protobuf:"bytes,2,opt,name=scm,proto3" json:"scm,omitempty"`
	XXX_NoUnkeyedLiteral struct{}        `json:"-"`
	XXX_unrecognized     []byte          `json:"-"`
	XXX_sizecache        int32           `json:"-"`
}

func (m *StoragePrepareReq) Reset()         { *m = StoragePrepareReq{} }
func (m *StoragePrepareReq) String() string { return proto.CompactTextString(m) }
func (*StoragePrepareReq) ProtoMessage()    {}
func (*StoragePrepareReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_3844f93d44b0acdf, []int{0}
}

func (m *StoragePrepareReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_StoragePrepareReq.Unmarshal(m, b)
}
func (m *StoragePrepareReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_StoragePrepareReq.Marshal(b, m, deterministic)
}
func (m *StoragePrepareReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_StoragePrepareReq.Merge(m, src)
}
func (m *StoragePrepareReq) XXX_Size() int {
	return xxx_messageInfo_StoragePrepareReq.Size(m)
}
func (m *StoragePrepareReq) XXX_DiscardUnknown() {
	xxx_messageInfo_StoragePrepareReq.DiscardUnknown(m)
}

var xxx_messageInfo_StoragePrepareReq proto.InternalMessageInfo

func (m *StoragePrepareReq) GetNvme() *PrepareNvmeReq {
	if m != nil {
		return m.Nvme
	}
	return nil
}

func (m *StoragePrepareReq) GetScm() *PrepareScmReq {
	if m != nil {
		return m.Scm
	}
	return nil
}

type StoragePrepareResp struct {
	Nvme                 *PrepareNvmeResp `protobuf:"bytes,1,opt,name=nvme,proto3" json:"nvme,omitempty"`
	Scm                  *PrepareScmResp  `protobuf:"bytes,2,opt,name=scm,proto3" json:"scm,omitempty"`
	XXX_NoUnkeyedLiteral struct{}         `json:"-"`
	XXX_unrecognized     []byte           `json:"-"`
	XXX_sizecache        int32            `json:"-"`
}

func (m *StoragePrepareResp) Reset()         { *m = StoragePrepareResp{} }
func (m *StoragePrepareResp) String() string { return proto.CompactTextString(m) }
func (*StoragePrepareResp) ProtoMessage()    {}
func (*StoragePrepareResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_3844f93d44b0acdf, []int{1}
}

func (m *StoragePrepareResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_StoragePrepareResp.Unmarshal(m, b)
}
func (m *StoragePrepareResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_StoragePrepareResp.Marshal(b, m, deterministic)
}
func (m *StoragePrepareResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_StoragePrepareResp.Merge(m, src)
}
func (m *StoragePrepareResp) XXX_Size() int {
	return xxx_messageInfo_StoragePrepareResp.Size(m)
}
func (m *StoragePrepareResp) XXX_DiscardUnknown() {
	xxx_messageInfo_StoragePrepareResp.DiscardUnknown(m)
}

var xxx_messageInfo_StoragePrepareResp proto.InternalMessageInfo

func (m *StoragePrepareResp) GetNvme() *PrepareNvmeResp {
	if m != nil {
		return m.Nvme
	}
	return nil
}

func (m *StoragePrepareResp) GetScm() *PrepareScmResp {
	if m != nil {
		return m.Scm
	}
	return nil
}

type StorageScanReq struct {
	Nvme                 *ScanNvmeReq `protobuf:"bytes,1,opt,name=nvme,proto3" json:"nvme,omitempty"`
	Scm                  *ScanScmReq  `protobuf:"bytes,2,opt,name=scm,proto3" json:"scm,omitempty"`
	XXX_NoUnkeyedLiteral struct{}     `json:"-"`
	XXX_unrecognized     []byte       `json:"-"`
	XXX_sizecache        int32        `json:"-"`
}

func (m *StorageScanReq) Reset()         { *m = StorageScanReq{} }
func (m *StorageScanReq) String() string { return proto.CompactTextString(m) }
func (*StorageScanReq) ProtoMessage()    {}
func (*StorageScanReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_3844f93d44b0acdf, []int{2}
}

func (m *StorageScanReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_StorageScanReq.Unmarshal(m, b)
}
func (m *StorageScanReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_StorageScanReq.Marshal(b, m, deterministic)
}
func (m *StorageScanReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_StorageScanReq.Merge(m, src)
}
func (m *StorageScanReq) XXX_Size() int {
	return xxx_messageInfo_StorageScanReq.Size(m)
}
func (m *StorageScanReq) XXX_DiscardUnknown() {
	xxx_messageInfo_StorageScanReq.DiscardUnknown(m)
}

var xxx_messageInfo_StorageScanReq proto.InternalMessageInfo

func (m *StorageScanReq) GetNvme() *ScanNvmeReq {
	if m != nil {
		return m.Nvme
	}
	return nil
}

func (m *StorageScanReq) GetScm() *ScanScmReq {
	if m != nil {
		return m.Scm
	}
	return nil
}

type StorageScanResp struct {
	Nvme                 *ScanNvmeResp `protobuf:"bytes,1,opt,name=nvme,proto3" json:"nvme,omitempty"`
	Scm                  *ScanScmResp  `protobuf:"bytes,2,opt,name=scm,proto3" json:"scm,omitempty"`
	XXX_NoUnkeyedLiteral struct{}      `json:"-"`
	XXX_unrecognized     []byte        `json:"-"`
	XXX_sizecache        int32         `json:"-"`
}

func (m *StorageScanResp) Reset()         { *m = StorageScanResp{} }
func (m *StorageScanResp) String() string { return proto.CompactTextString(m) }
func (*StorageScanResp) ProtoMessage()    {}
func (*StorageScanResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_3844f93d44b0acdf, []int{3}
}

func (m *StorageScanResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_StorageScanResp.Unmarshal(m, b)
}
func (m *StorageScanResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_StorageScanResp.Marshal(b, m, deterministic)
}
func (m *StorageScanResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_StorageScanResp.Merge(m, src)
}
func (m *StorageScanResp) XXX_Size() int {
	return xxx_messageInfo_StorageScanResp.Size(m)
}
func (m *StorageScanResp) XXX_DiscardUnknown() {
	xxx_messageInfo_StorageScanResp.DiscardUnknown(m)
}

var xxx_messageInfo_StorageScanResp proto.InternalMessageInfo

func (m *StorageScanResp) GetNvme() *ScanNvmeResp {
	if m != nil {
		return m.Nvme
	}
	return nil
}

func (m *StorageScanResp) GetScm() *ScanScmResp {
	if m != nil {
		return m.Scm
	}
	return nil
}

type StorageFormatReq struct {
	Nvme                 *FormatNvmeReq `protobuf:"bytes,1,opt,name=nvme,proto3" json:"nvme,omitempty"`
	Scm                  *FormatScmReq  `protobuf:"bytes,2,opt,name=scm,proto3" json:"scm,omitempty"`
	Reformat             bool           `protobuf:"varint,3,opt,name=reformat,proto3" json:"reformat,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *StorageFormatReq) Reset()         { *m = StorageFormatReq{} }
func (m *StorageFormatReq) String() string { return proto.CompactTextString(m) }
func (*StorageFormatReq) ProtoMessage()    {}
func (*StorageFormatReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_3844f93d44b0acdf, []int{4}
}

func (m *StorageFormatReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_StorageFormatReq.Unmarshal(m, b)
}
func (m *StorageFormatReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_StorageFormatReq.Marshal(b, m, deterministic)
}
func (m *StorageFormatReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_StorageFormatReq.Merge(m, src)
}
func (m *StorageFormatReq) XXX_Size() int {
	return xxx_messageInfo_StorageFormatReq.Size(m)
}
func (m *StorageFormatReq) XXX_DiscardUnknown() {
	xxx_messageInfo_StorageFormatReq.DiscardUnknown(m)
}

var xxx_messageInfo_StorageFormatReq proto.InternalMessageInfo

func (m *StorageFormatReq) GetNvme() *FormatNvmeReq {
	if m != nil {
		return m.Nvme
	}
	return nil
}

func (m *StorageFormatReq) GetScm() *FormatScmReq {
	if m != nil {
		return m.Scm
	}
	return nil
}

func (m *StorageFormatReq) GetReformat() bool {
	if m != nil {
		return m.Reformat
	}
	return false
}

type StorageFormatResp struct {
	Crets                []*NvmeControllerResult `protobuf:"bytes,1,rep,name=crets,proto3" json:"crets,omitempty"`
	Mrets                []*ScmMountResult       `protobuf:"bytes,2,rep,name=mrets,proto3" json:"mrets,omitempty"`
	XXX_NoUnkeyedLiteral struct{}                `json:"-"`
	XXX_unrecognized     []byte                  `json:"-"`
	XXX_sizecache        int32                   `json:"-"`
}

func (m *StorageFormatResp) Reset()         { *m = StorageFormatResp{} }
func (m *StorageFormatResp) String() string { return proto.CompactTextString(m) }
func (*StorageFormatResp) ProtoMessage()    {}
func (*StorageFormatResp) Descriptor() ([]byte, []int) {
	return fileDescriptor_3844f93d44b0acdf, []int{5}
}

func (m *StorageFormatResp) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_StorageFormatResp.Unmarshal(m, b)
}
func (m *StorageFormatResp) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_StorageFormatResp.Marshal(b, m, deterministic)
}
func (m *StorageFormatResp) XXX_Merge(src proto.Message) {
	xxx_messageInfo_StorageFormatResp.Merge(m, src)
}
func (m *StorageFormatResp) XXX_Size() int {
	return xxx_messageInfo_StorageFormatResp.Size(m)
}
func (m *StorageFormatResp) XXX_DiscardUnknown() {
	xxx_messageInfo_StorageFormatResp.DiscardUnknown(m)
}

var xxx_messageInfo_StorageFormatResp proto.InternalMessageInfo

func (m *StorageFormatResp) GetCrets() []*NvmeControllerResult {
	if m != nil {
		return m.Crets
	}
	return nil
}

func (m *StorageFormatResp) GetMrets() []*ScmMountResult {
	if m != nil {
		return m.Mrets
	}
	return nil
}

func init() {
	proto.RegisterType((*StoragePrepareReq)(nil), "ctl.StoragePrepareReq")
	proto.RegisterType((*StoragePrepareResp)(nil), "ctl.StoragePrepareResp")
	proto.RegisterType((*StorageScanReq)(nil), "ctl.StorageScanReq")
	proto.RegisterType((*StorageScanResp)(nil), "ctl.StorageScanResp")
	proto.RegisterType((*StorageFormatReq)(nil), "ctl.StorageFormatReq")
	proto.RegisterType((*StorageFormatResp)(nil), "ctl.StorageFormatResp")
}

func init() {
	proto.RegisterFile("ctl/storage.proto", fileDescriptor_3844f93d44b0acdf)
}

var fileDescriptor_3844f93d44b0acdf = []byte{
	// 367 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x64, 0x92, 0x41, 0x4f, 0xe3, 0x30,
	0x10, 0x85, 0x95, 0x66, 0xbb, 0xaa, 0x5c, 0x69, 0xdb, 0xb8, 0x0b, 0x0a, 0x3d, 0x95, 0xd0, 0x42,
	0x38, 0x10, 0x4b, 0xe5, 0x80, 0xb8, 0x82, 0xc4, 0x0d, 0x84, 0xd2, 0x13, 0x08, 0x09, 0xa5, 0x83,
	0x29, 0x88, 0x38, 0x0e, 0xb6, 0xdb, 0x0b, 0x7f, 0x1e, 0xd9, 0x4e, 0x23, 0xbb, 0xbd, 0x39, 0x33,
	0x6f, 0xde, 0xf7, 0x26, 0x1a, 0x14, 0x81, 0x2a, 0x89, 0x54, 0x5c, 0x14, 0x2b, 0x9a, 0xd5, 0x82,
	0x2b, 0x8e, 0x43, 0x50, 0xe5, 0xf8, 0xd0, 0xa9, 0xbf, 0x56, 0x1b, 0xd6, 0x34, 0xc7, 0x07, 0x6e,
	0x5d, 0x02, 0xb3, 0xe5, 0x64, 0x89, 0xa2, 0x85, 0x2d, 0x3e, 0x0a, 0x5a, 0x17, 0x82, 0xe6, 0xf4,
	0x1b, 0x9f, 0xa1, 0x3f, 0x7a, 0x32, 0x0e, 0x26, 0x41, 0xda, 0x9f, 0x8f, 0x32, 0x50, 0x65, 0xd6,
	0xb4, 0x1f, 0x36, 0x4c, 0x4b, 0x72, 0x23, 0xc0, 0x53, 0x14, 0x4a, 0x60, 0x71, 0xc7, 0xe8, 0xb0,
	0xab, 0x5b, 0x00, 0xd3, 0x32, 0xdd, 0x4e, 0x28, 0xc2, 0xbb, 0x0c, 0x59, 0xe3, 0xd4, 0x83, 0xfc,
	0xdf, 0x87, 0xc8, 0xba, 0xa1, 0xcc, 0x5c, 0xca, 0x68, 0x8f, 0x22, 0x6b, 0x8b, 0x79, 0x42, 0xff,
	0x1a, 0xcc, 0x02, 0x8a, 0x4a, 0xef, 0x31, 0xf5, 0x10, 0x43, 0x33, 0xa9, 0x7b, 0xfe, 0x12, 0xc7,
	0xae, 0xfd, 0xa0, 0x15, 0xb9, 0x1b, 0xbc, 0xa0, 0x81, 0x67, 0x2d, 0x6b, 0x3c, 0xf3, 0xbc, 0xa3,
	0x1d, 0xef, 0x36, 0x7b, 0xe2, 0x9a, 0x0f, 0x7d, 0xf3, 0x6d, 0xf0, 0x1f, 0x34, 0x6c, 0xdc, 0xef,
	0xb8, 0x60, 0x85, 0xd2, 0xd1, 0x4f, 0x3d, 0x7b, 0xfb, 0x6b, 0x6d, 0xd7, 0x0f, 0x7f, 0xe2, 0xfa,
	0x47, 0x8e, 0xcc, 0x89, 0x8f, 0xc7, 0xa8, 0x27, 0xe8, 0xbb, 0x29, 0xc7, 0xe1, 0x24, 0x48, 0x7b,
	0x79, 0xfb, 0x9d, 0xf0, 0xf6, 0x00, 0xb6, 0x70, 0x59, 0x63, 0x82, 0xba, 0x20, 0xa8, 0x92, 0x71,
	0x30, 0x09, 0xd3, 0xfe, 0xfc, 0xc8, 0xf8, 0x6a, 0xf0, 0x2d, 0xaf, 0x94, 0xe0, 0x65, 0x49, 0x45,
	0x4e, 0xe5, 0xba, 0x54, 0xb9, 0xd5, 0xe1, 0x73, 0xd4, 0x65, 0x66, 0xa0, 0x63, 0x06, 0x46, 0xcd,
	0xa2, 0xec, 0x9e, 0xaf, 0x2b, 0xb5, 0x95, 0x1a, 0xc5, 0xcd, 0xf5, 0xf3, 0xd5, 0xea, 0x53, 0x7d,
	0xac, 0x97, 0x19, 0x70, 0x46, 0xde, 0x0a, 0x2e, 0x2f, 0xa4, 0x2a, 0xe0, 0xcb, 0x3c, 0x89, 0x14,
	0x40, 0xc0, 0x42, 0x08, 0x70, 0xc6, 0x78, 0x45, 0xcc, 0xa1, 0x12, 0x50, 0xe5, 0xf2, 0xaf, 0x79,
	0x5e, 0xfe, 0x06, 0x00, 0x00, 0xff, 0xff, 0xbd, 0x9a, 0x58, 0x5f, 0xfc, 0x02, 0x00, 0x00,
}
