// Code generated by protoc-gen-go. DO NOT EDIT.
// source: common.proto

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

type ResponseStatus int32

const (
	ResponseStatus_CTRL_SUCCESS     ResponseStatus = 0
	ResponseStatus_CTRL_IN_PROGRESS ResponseStatus = 1
	ResponseStatus_CTRL_WAITING     ResponseStatus = 2
	ResponseStatus_CTRL_ERR_CONF    ResponseStatus = -1
	ResponseStatus_CTRL_ERR_NVME    ResponseStatus = -2
	ResponseStatus_CTRL_ERR_SCM     ResponseStatus = -3
	ResponseStatus_CTRL_ERR_APP     ResponseStatus = -4
	ResponseStatus_CTRL_ERR_UNKNOWN ResponseStatus = -5
	ResponseStatus_CTRL_NO_IMPL     ResponseStatus = -6
)

var ResponseStatus_name = map[int32]string{
	0:  "CTRL_SUCCESS",
	1:  "CTRL_IN_PROGRESS",
	2:  "CTRL_WAITING",
	-1: "CTRL_ERR_CONF",
	-2: "CTRL_ERR_NVME",
	-3: "CTRL_ERR_SCM",
	-4: "CTRL_ERR_APP",
	-5: "CTRL_ERR_UNKNOWN",
	-6: "CTRL_NO_IMPL",
}
var ResponseStatus_value = map[string]int32{
	"CTRL_SUCCESS":     0,
	"CTRL_IN_PROGRESS": 1,
	"CTRL_WAITING":     2,
	"CTRL_ERR_CONF":    -1,
	"CTRL_ERR_NVME":    -2,
	"CTRL_ERR_SCM":     -3,
	"CTRL_ERR_APP":     -4,
	"CTRL_ERR_UNKNOWN": -5,
	"CTRL_NO_IMPL":     -6,
}

func (x ResponseStatus) String() string {
	return proto.EnumName(ResponseStatus_name, int32(x))
}
func (ResponseStatus) EnumDescriptor() ([]byte, []int) {
	return fileDescriptor_common_b4acff35d6b9d00d, []int{0}
}

type EmptyReq struct {
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *EmptyReq) Reset()         { *m = EmptyReq{} }
func (m *EmptyReq) String() string { return proto.CompactTextString(m) }
func (*EmptyReq) ProtoMessage()    {}
func (*EmptyReq) Descriptor() ([]byte, []int) {
	return fileDescriptor_common_b4acff35d6b9d00d, []int{0}
}
func (m *EmptyReq) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_EmptyReq.Unmarshal(m, b)
}
func (m *EmptyReq) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_EmptyReq.Marshal(b, m, deterministic)
}
func (dst *EmptyReq) XXX_Merge(src proto.Message) {
	xxx_messageInfo_EmptyReq.Merge(dst, src)
}
func (m *EmptyReq) XXX_Size() int {
	return xxx_messageInfo_EmptyReq.Size(m)
}
func (m *EmptyReq) XXX_DiscardUnknown() {
	xxx_messageInfo_EmptyReq.DiscardUnknown(m)
}

var xxx_messageInfo_EmptyReq proto.InternalMessageInfo

type FilePath struct {
	Path                 string   `protobuf:"bytes,1,opt,name=path,proto3" json:"path,omitempty"`
	XXX_NoUnkeyedLiteral struct{} `json:"-"`
	XXX_unrecognized     []byte   `json:"-"`
	XXX_sizecache        int32    `json:"-"`
}

func (m *FilePath) Reset()         { *m = FilePath{} }
func (m *FilePath) String() string { return proto.CompactTextString(m) }
func (*FilePath) ProtoMessage()    {}
func (*FilePath) Descriptor() ([]byte, []int) {
	return fileDescriptor_common_b4acff35d6b9d00d, []int{1}
}
func (m *FilePath) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_FilePath.Unmarshal(m, b)
}
func (m *FilePath) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_FilePath.Marshal(b, m, deterministic)
}
func (dst *FilePath) XXX_Merge(src proto.Message) {
	xxx_messageInfo_FilePath.Merge(dst, src)
}
func (m *FilePath) XXX_Size() int {
	return xxx_messageInfo_FilePath.Size(m)
}
func (m *FilePath) XXX_DiscardUnknown() {
	xxx_messageInfo_FilePath.DiscardUnknown(m)
}

var xxx_messageInfo_FilePath proto.InternalMessageInfo

func (m *FilePath) GetPath() string {
	if m != nil {
		return m.Path
	}
	return ""
}

type ResponseState struct {
	Status               ResponseStatus `protobuf:"varint,1,opt,name=status,proto3,enum=mgmt.ResponseStatus" json:"status,omitempty"`
	Error                string         `protobuf:"bytes,2,opt,name=error,proto3" json:"error,omitempty"`
	Info                 string         `protobuf:"bytes,3,opt,name=info,proto3" json:"info,omitempty"`
	XXX_NoUnkeyedLiteral struct{}       `json:"-"`
	XXX_unrecognized     []byte         `json:"-"`
	XXX_sizecache        int32          `json:"-"`
}

func (m *ResponseState) Reset()         { *m = ResponseState{} }
func (m *ResponseState) String() string { return proto.CompactTextString(m) }
func (*ResponseState) ProtoMessage()    {}
func (*ResponseState) Descriptor() ([]byte, []int) {
	return fileDescriptor_common_b4acff35d6b9d00d, []int{2}
}
func (m *ResponseState) XXX_Unmarshal(b []byte) error {
	return xxx_messageInfo_ResponseState.Unmarshal(m, b)
}
func (m *ResponseState) XXX_Marshal(b []byte, deterministic bool) ([]byte, error) {
	return xxx_messageInfo_ResponseState.Marshal(b, m, deterministic)
}
func (dst *ResponseState) XXX_Merge(src proto.Message) {
	xxx_messageInfo_ResponseState.Merge(dst, src)
}
func (m *ResponseState) XXX_Size() int {
	return xxx_messageInfo_ResponseState.Size(m)
}
func (m *ResponseState) XXX_DiscardUnknown() {
	xxx_messageInfo_ResponseState.DiscardUnknown(m)
}

var xxx_messageInfo_ResponseState proto.InternalMessageInfo

func (m *ResponseState) GetStatus() ResponseStatus {
	if m != nil {
		return m.Status
	}
	return ResponseStatus_CTRL_SUCCESS
}

func (m *ResponseState) GetError() string {
	if m != nil {
		return m.Error
	}
	return ""
}

func (m *ResponseState) GetInfo() string {
	if m != nil {
		return m.Info
	}
	return ""
}

func init() {
	proto.RegisterType((*EmptyReq)(nil), "mgmt.EmptyReq")
	proto.RegisterType((*FilePath)(nil), "mgmt.FilePath")
	proto.RegisterType((*ResponseState)(nil), "mgmt.ResponseState")
	proto.RegisterEnum("mgmt.ResponseStatus", ResponseStatus_name, ResponseStatus_value)
}

func init() { proto.RegisterFile("common.proto", fileDescriptor_common_b4acff35d6b9d00d) }

var fileDescriptor_common_b4acff35d6b9d00d = []byte{
	// 285 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x6c, 0x91, 0x5f, 0x4b, 0xc3, 0x30,
	0x14, 0xc5, 0xed, 0x9c, 0x63, 0x5e, 0xb6, 0x71, 0x09, 0x7b, 0xa8, 0x82, 0x22, 0x7d, 0x12, 0x91,
	0x3e, 0xe8, 0x27, 0x18, 0xa5, 0x1b, 0xc5, 0x35, 0x2d, 0xe9, 0xe6, 0x1e, 0x4b, 0x95, 0xb8, 0x0d,
	0x6c, 0x53, 0x9b, 0xec, 0xc1, 0xaf, 0xed, 0xa3, 0xff, 0x49, 0x56, 0x37, 0x87, 0xe6, 0xe9, 0xe6,
	0xfc, 0x4e, 0xce, 0x09, 0x5c, 0xe8, 0xdc, 0x8b, 0x3c, 0x17, 0x85, 0x5b, 0x56, 0x42, 0x09, 0xd2,
	0xcc, 0xe7, 0xb9, 0x72, 0x00, 0xda, 0x7e, 0x5e, 0xaa, 0x67, 0xc6, 0x9f, 0x9c, 0x53, 0x68, 0x0f,
	0x97, 0x8f, 0x3c, 0xce, 0xd4, 0x82, 0x10, 0x68, 0x96, 0x99, 0x5a, 0xd8, 0xd6, 0x99, 0x75, 0x7e,
	0xc8, 0xcc, 0xec, 0xcc, 0xa1, 0xcb, 0xb8, 0x2c, 0x45, 0x21, 0x79, 0xa2, 0x32, 0xc5, 0xc9, 0x25,
	0xb4, 0xa4, 0xca, 0xd4, 0x4a, 0x1a, 0x5b, 0xef, 0xaa, 0xef, 0xea, 0x4c, 0xf7, 0xb7, 0x69, 0x25,
	0x59, 0xed, 0x21, 0x7d, 0x38, 0xe0, 0x55, 0x25, 0x2a, 0xbb, 0x61, 0x32, 0xd7, 0x17, 0x5d, 0xb4,
	0x2c, 0x1e, 0x84, 0xbd, 0xbf, 0x2e, 0xd2, 0xf3, 0xc5, 0x8b, 0x05, 0xbd, 0xdd, 0x10, 0x82, 0xd0,
	0xf1, 0x26, 0x6c, 0x9c, 0x26, 0x53, 0xcf, 0xf3, 0x93, 0x04, 0xf7, 0x48, 0x1f, 0xd0, 0x28, 0x01,
	0x4d, 0x63, 0x16, 0x8d, 0x98, 0x56, 0xad, 0x8d, 0x6f, 0x36, 0x08, 0x26, 0x01, 0x1d, 0x61, 0x83,
	0x1c, 0x43, 0xd7, 0x28, 0x3e, 0x63, 0xa9, 0x17, 0xd1, 0x21, 0x7e, 0xfd, 0x1c, 0x6b, 0x87, 0xd1,
	0xdb, 0xd0, 0xc7, 0xcf, 0x2d, 0x3b, 0xaa, 0x93, 0x34, 0x4b, 0xbc, 0x10, 0x3f, 0xfe, 0x47, 0x83,
	0x38, 0xc6, 0xf7, 0x2d, 0x3a, 0xa9, 0x7f, 0xa5, 0xd1, 0x94, 0xde, 0xd0, 0x68, 0x46, 0xf1, 0xed,
	0xef, 0x4b, 0x1a, 0xa5, 0x41, 0x18, 0x8f, 0xf1, 0x75, 0x83, 0xee, 0x5a, 0x66, 0x2d, 0xd7, 0xdf,
	0x01, 0x00, 0x00, 0xff, 0xff, 0x86, 0x26, 0x10, 0x67, 0xa6, 0x01, 0x00, 0x00,
}
