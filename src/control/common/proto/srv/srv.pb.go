// Code generated by protoc-gen-go. DO NOT EDIT.
// source: srv.proto

package srv

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

type NotifyReadyReq struct {
	Uri                  string   `protobuf:"bytes,1,opt,name=uri,proto3" json:"uri,omitempty"`
	Nctxs                uint32   `protobuf:"varint,2,opt,name=nctxs,proto3" json:"nctxs,omitempty"`
	DrpcListenerSock     string   `protobuf:"bytes,3,opt,name=drpcListenerSock,proto3" json:"drpcListenerSock,omitempty"`
	InstanceIdx          uint32   `protobuf:"varint,4,opt,name=instanceIdx,proto3" json:"instanceIdx,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *NotifyReadyReq) Reset()         { *m = NotifyReadyReq{} }
func (m *NotifyReadyReq) String() string { return proto.CompactTextString(m) }
func (*NotifyReadyReq) ProtoMessage()    {}
func (*NotifyReadyReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_srv_e172066199c874cc, []int{0}
}
func (m *NotifyReadyReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_NotifyReadyReq.Unmarshal(m, b)
}
func (m *NotifyReadyReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_NotifyReadyReq.Marshal(b, m, deterministic)
}
func (dst *NotifyReadyReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_NotifyReadyReq.Merge(dst, src)
}
func (m *NotifyReadyReq) XXX_Size() int {
	return xxx_messageInfo_NotifyReadyReq.Size(m)
}
func (m *NotifyReadyReq) XXX_DiscardUnknown() {
	xxx_messageInfo_NotifyReadyReq.DiscardUnknown(m)
}

var xxx_messageInfo_NotifyReadyReq proto.InternalMessageInfo

func (m *NotifyReadyReq) GetUri() string {
	if m != nil {
		return m.Uri
	}
	return ""
}

func (m *NotifyReadyReq) GetNctxs() uint32 {
	if m != nil {
		return m.Nctxs
	}
	return 0
}

func (m *NotifyReadyReq) GetDrpcListenerSock() string {
	if m != nil {
		return m.DrpcListenerSock
	}
	return ""
}

func (m *NotifyReadyReq) GetInstanceIdx() uint32 {
	if m != nil {
		return m.InstanceIdx
	}
	return 0
}

type BioErrorReq struct {
	UnmapErr             bool     `protobuf:"varint,1,opt,name=unmapErr,proto3" json:"unmapErr,omitempty"`
	ReadErr              bool     `protobuf:"varint,2,opt,name=readErr,proto3" json:"readErr,omitempty"`
	WriteErr             bool     `protobuf:"varint,3,opt,name=writeErr,proto3" json:"writeErr,omitempty"`
	TgtId                int32    `protobuf:"varint,4,opt,name=tgtId,proto3" json:"tgtId,omitempty"`
	InstanceIdx          uint32   `protobuf:"varint,5,opt,name=instanceIdx,proto3" json:"instanceIdx,omitempty"`
	DrpcListenerSock     string   `protobuf:"bytes,6,opt,name=drpcListenerSock,proto3" json:"drpcListenerSock,omitempty"`
	Uri                  string   `protobuf:"bytes,7,opt,name=uri,proto3" json:"uri,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *BioErrorReq) Reset()         { *m = BioErrorReq{} }
func (m *BioErrorReq) String() string { return proto.CompactTextString(m) }
func (*BioErrorReq) ProtoMessage()    {}
func (*BioErrorReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_srv_e172066199c874cc, []int{1}
}
func (m *BioErrorReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_BioErrorReq.Unmarshal(m, b)
}
func (m *BioErrorReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_BioErrorReq.Marshal(b, m, deterministic)
}
func (dst *BioErrorReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_BioErrorReq.Merge(dst, src)
}
func (m *BioErrorReq) XXX_Size() int {
	return xxx_messageInfo_BioErrorReq.Size(m)
}
func (m *BioErrorReq) XXX_DiscardUnknown() {
	xxx_messageInfo_BioErrorReq.DiscardUnknown(m)
}

var xxx_messageInfo_BioErrorReq proto.InternalMessageInfo

func (m *BioErrorReq) GetUnmapErr() bool {
	if m != nil {
		return m.UnmapErr
	}
	return false
}

func (m *BioErrorReq) GetReadErr() bool {
	if m != nil {
		return m.ReadErr
	}
	return false
}

func (m *BioErrorReq) GetWriteErr() bool {
	if m != nil {
		return m.WriteErr
	}
	return false
}

func (m *BioErrorReq) GetTgtId() int32 {
	if m != nil {
		return m.TgtId
	}
	return 0
}

func (m *BioErrorReq) GetInstanceIdx() uint32 {
	if m != nil {
		return m.InstanceIdx
	}
	return 0
}

func (m *BioErrorReq) GetDrpcListenerSock() string {
	if m != nil {
		return m.DrpcListenerSock
	}
	return ""
}

func (m *BioErrorReq) GetUri() string {
	if m != nil {
		return m.Uri
	}
	return ""
}

func init() {
	proto.RegisterType((*NotifyReadyReq)(nil), "srv.NotifyReadyReq")
	proto.RegisterType((*BioErrorReq)(nil), "srv.BioErrorReq")
}

func init() { proto.RegisterFile("srv.proto", fileDescriptor_srv_e172066199c874cc) }

var fileDescriptor_srv_e172066199c874cc = []byte{
	// 236 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xe2, 0xe2, 0x2c, 0x2e, 0x2a, 0xd3,
	0x2b, 0x28, 0xca, 0x2f, 0xc9, 0x17, 0x62, 0x2e, 0x2e, 0x2a, 0x53, 0x6a, 0x63, 0xe4, 0xe2, 0xf3,
	0xcb, 0x2f, 0xc9, 0x4c, 0xab, 0x0c, 0x4a, 0x4d, 0x4c, 0xa9, 0x0c, 0x4a, 0x2d, 0x14, 0x12, 0xe0,
	0x62, 0x2e, 0x2d, 0xca, 0x94, 0x60, 0x54, 0x60, 0xd4, 0xe0, 0x0c, 0x02, 0x31, 0x85, 0x44, 0xb8,
	0x58, 0xf3, 0x92, 0x4b, 0x2a, 0x8a, 0x25, 0x98, 0x14, 0x18, 0x35, 0x78, 0x83, 0x20, 0x1c, 0x21,
	0x2d, 0x2e, 0x81, 0x94, 0xa2, 0x82, 0x64, 0x9f, 0xcc, 0xe2, 0x92, 0xd4, 0xbc, 0xd4, 0xa2, 0xe0,
	0xfc, 0xe4, 0x6c, 0x09, 0x66, 0xb0, 0x26, 0x0c, 0x71, 0x21, 0x05, 0x2e, 0xee, 0xcc, 0xbc, 0xe2,
	0x92, 0xc4, 0xbc, 0xe4, 0x54, 0xcf, 0x94, 0x0a, 0x09, 0x16, 0xb0, 0x39, 0xc8, 0x42, 0x4a, 0x57,
	0x19, 0xb9, 0xb8, 0x9d, 0x32, 0xf3, 0x5d, 0x8b, 0x8a, 0xf2, 0x8b, 0x40, 0xae, 0x90, 0xe2, 0xe2,
	0x28, 0xcd, 0xcb, 0x4d, 0x2c, 0x70, 0x2d, 0x2a, 0x02, 0x3b, 0x85, 0x23, 0x08, 0xce, 0x17, 0x92,
	0xe0, 0x62, 0x2f, 0x4a, 0x4d, 0x4c, 0x01, 0x49, 0x31, 0x81, 0xa5, 0x60, 0x5c, 0x90, 0xae, 0xf2,
	0xa2, 0xcc, 0x92, 0x54, 0x90, 0x14, 0x33, 0x44, 0x17, 0x8c, 0x0f, 0xf2, 0x45, 0x49, 0x7a, 0x89,
	0x67, 0x0a, 0xd8, 0x76, 0xd6, 0x20, 0x08, 0x07, 0xdd, 0x65, 0xac, 0x18, 0x2e, 0xc3, 0xea, 0x4f,
	0x36, 0x1c, 0xfe, 0x84, 0x86, 0x1d, 0x3b, 0x3c, 0xec, 0x92, 0xd8, 0xc0, 0x81, 0x6d, 0x0c, 0x08,
	0x00, 0x00, 0xff, 0xff, 0xce, 0x6a, 0xec, 0x62, 0x79, 0x01, 0x00, 0x00,
}
