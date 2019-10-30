// Code generated by protoc-gen-go. DO NOT EDIT.
// source: mgmt.proto

package mgmt

import proto "github.com/golang/protobuf/proto"
import fmt "fmt"
import math "math"

import (
	context "golang.org/x/net/context"
	grpc "google.golang.org/grpc"
)

// Reference imports to suppress errors if they are not otherwise used.
var _ = proto.Marshal
var _ = fmt.Errorf
var _ = math.Inf

// This is a compile-time assertion to ensure that this generated file
// is compatible with the proto package it is being compiled against.
// A compilation error at this line likely means your copy of the
// proto package needs to be updated.
const _ = proto.ProtoPackageIsVersion2 // please upgrade the proto package

// Reference imports to suppress errors if they are not otherwise used.
var _ context.Context
var _ grpc.ClientConn

// This is a compile-time assertion to ensure that this generated file
// is compatible with the grpc package it is being compiled against.
const _ = grpc.SupportPackageIsVersion4

// MgmtSvcClient is the client API for MgmtSvc service.
//
// For semantics around ctx use and closing/ending streaming RPCs, please refer to https://godoc.org/google.golang.org/grpc#ClientConn.NewStream.
type MgmtSvcClient interface {
	// Join the server described by JoinReq to the system.
	Join(ctx context.Context, in *JoinReq, opts ...grpc.CallOption) (*JoinResp, error)
	// Create a DAOS pool allocated across a number of ranks
	PoolCreate(ctx context.Context, in *PoolCreateReq, opts ...grpc.CallOption) (*PoolCreateResp, error)
	// Destroy a DAOS pool allocated across a number of ranks
	PoolDestroy(ctx context.Context, in *PoolDestroyReq, opts ...grpc.CallOption) (*PoolDestroyResp, error)
	// Fetch the Access Control List for a DAOS pool
	PoolGetACL(ctx context.Context, in *GetACLReq, opts ...grpc.CallOption) (*GetACLResp, error)
	// Get the information required by libdaos to attach to the system.
	GetAttachInfo(ctx context.Context, in *GetAttachInfoReq, opts ...grpc.CallOption) (*GetAttachInfoResp, error)
	// Get BIO device health information
	BioHealthQuery(ctx context.Context, in *BioHealthReq, opts ...grpc.CallOption) (*BioHealthResp, error)
	// Get SMD device list
	SmdListDevs(ctx context.Context, in *SmdDevReq, opts ...grpc.CallOption) (*SmdDevResp, error)
	// Get SMD pool list
	SmdListPools(ctx context.Context, in *SmdPoolReq, opts ...grpc.CallOption) (*SmdPoolResp, error)
	// Kill DAOS IO server identified by rank.
	KillRank(ctx context.Context, in *KillRankReq, opts ...grpc.CallOption) (*DaosResp, error)
}

type mgmtSvcClient struct {
	cc *grpc.ClientConn
}

func NewMgmtSvcClient(cc *grpc.ClientConn) MgmtSvcClient {
	return &mgmtSvcClient{cc}
}

func (c *mgmtSvcClient) Join(ctx context.Context, in *JoinReq, opts ...grpc.CallOption) (*JoinResp, error) {
	out := new(JoinResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/Join", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) PoolCreate(ctx context.Context, in *PoolCreateReq, opts ...grpc.CallOption) (*PoolCreateResp, error) {
	out := new(PoolCreateResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/PoolCreate", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) PoolDestroy(ctx context.Context, in *PoolDestroyReq, opts ...grpc.CallOption) (*PoolDestroyResp, error) {
	out := new(PoolDestroyResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/PoolDestroy", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) PoolGetACL(ctx context.Context, in *GetACLReq, opts ...grpc.CallOption) (*GetACLResp, error) {
	out := new(GetACLResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/PoolGetACL", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) GetAttachInfo(ctx context.Context, in *GetAttachInfoReq, opts ...grpc.CallOption) (*GetAttachInfoResp, error) {
	out := new(GetAttachInfoResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/GetAttachInfo", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) BioHealthQuery(ctx context.Context, in *BioHealthReq, opts ...grpc.CallOption) (*BioHealthResp, error) {
	out := new(BioHealthResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/BioHealthQuery", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) SmdListDevs(ctx context.Context, in *SmdDevReq, opts ...grpc.CallOption) (*SmdDevResp, error) {
	out := new(SmdDevResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/SmdListDevs", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) SmdListPools(ctx context.Context, in *SmdPoolReq, opts ...grpc.CallOption) (*SmdPoolResp, error) {
	out := new(SmdPoolResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/SmdListPools", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) KillRank(ctx context.Context, in *KillRankReq, opts ...grpc.CallOption) (*DaosResp, error) {
	out := new(DaosResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/KillRank", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

// MgmtSvcServer is the server API for MgmtSvc service.
type MgmtSvcServer interface {
	// Join the server described by JoinReq to the system.
	Join(context.Context, *JoinReq) (*JoinResp, error)
	// Create a DAOS pool allocated across a number of ranks
	PoolCreate(context.Context, *PoolCreateReq) (*PoolCreateResp, error)
	// Destroy a DAOS pool allocated across a number of ranks
	PoolDestroy(context.Context, *PoolDestroyReq) (*PoolDestroyResp, error)
	// Fetch the Access Control List for a DAOS pool
	PoolGetACL(context.Context, *GetACLReq) (*GetACLResp, error)
	// Get the information required by libdaos to attach to the system.
	GetAttachInfo(context.Context, *GetAttachInfoReq) (*GetAttachInfoResp, error)
	// Get BIO device health information
	BioHealthQuery(context.Context, *BioHealthReq) (*BioHealthResp, error)
	// Get SMD device list
	SmdListDevs(context.Context, *SmdDevReq) (*SmdDevResp, error)
	// Get SMD pool list
	SmdListPools(context.Context, *SmdPoolReq) (*SmdPoolResp, error)
	// Kill DAOS IO server identified by rank.
	KillRank(context.Context, *KillRankReq) (*DaosResp, error)
}

func RegisterMgmtSvcServer(s *grpc.Server, srv MgmtSvcServer) {
	s.RegisterService(&_MgmtSvc_serviceDesc, srv)
}

func _MgmtSvc_Join_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(JoinReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).Join(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/Join",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).Join(ctx, req.(*JoinReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_PoolCreate_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(PoolCreateReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).PoolCreate(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/PoolCreate",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).PoolCreate(ctx, req.(*PoolCreateReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_PoolDestroy_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(PoolDestroyReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).PoolDestroy(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/PoolDestroy",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).PoolDestroy(ctx, req.(*PoolDestroyReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_PoolGetACL_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(GetACLReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).PoolGetACL(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/PoolGetACL",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).PoolGetACL(ctx, req.(*GetACLReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_GetAttachInfo_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(GetAttachInfoReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).GetAttachInfo(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/GetAttachInfo",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).GetAttachInfo(ctx, req.(*GetAttachInfoReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_BioHealthQuery_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(BioHealthReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).BioHealthQuery(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/BioHealthQuery",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).BioHealthQuery(ctx, req.(*BioHealthReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_SmdListDevs_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(SmdDevReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).SmdListDevs(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/SmdListDevs",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).SmdListDevs(ctx, req.(*SmdDevReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_SmdListPools_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(SmdPoolReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).SmdListPools(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/SmdListPools",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).SmdListPools(ctx, req.(*SmdPoolReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_KillRank_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(KillRankReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).KillRank(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/KillRank",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).KillRank(ctx, req.(*KillRankReq))
	}
	return interceptor(ctx, in, info, handler)
}

var _MgmtSvc_serviceDesc = grpc.ServiceDesc{
	ServiceName: "mgmt.MgmtSvc",
	HandlerType: (*MgmtSvcServer)(nil),
	Methods: []grpc.MethodDesc{
		{
			MethodName: "Join",
			Handler:    _MgmtSvc_Join_Handler,
		},
		{
			MethodName: "PoolCreate",
			Handler:    _MgmtSvc_PoolCreate_Handler,
		},
		{
			MethodName: "PoolDestroy",
			Handler:    _MgmtSvc_PoolDestroy_Handler,
		},
		{
			MethodName: "PoolGetACL",
			Handler:    _MgmtSvc_PoolGetACL_Handler,
		},
		{
			MethodName: "GetAttachInfo",
			Handler:    _MgmtSvc_GetAttachInfo_Handler,
		},
		{
			MethodName: "BioHealthQuery",
			Handler:    _MgmtSvc_BioHealthQuery_Handler,
		},
		{
			MethodName: "SmdListDevs",
			Handler:    _MgmtSvc_SmdListDevs_Handler,
		},
		{
			MethodName: "SmdListPools",
			Handler:    _MgmtSvc_SmdListPools_Handler,
		},
		{
			MethodName: "KillRank",
			Handler:    _MgmtSvc_KillRank_Handler,
		},
	},
	Streams:  []grpc.StreamDesc{},
	Metadata: "mgmt.proto",
}

func init() { proto.RegisterFile("mgmt.proto", fileDescriptor_mgmt_c09b0d287035ce55) }

var fileDescriptor_mgmt_c09b0d287035ce55 = []byte{
	// 305 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x6c, 0x92, 0x4b, 0x4f, 0xf2, 0x40,
	0x14, 0x86, 0xbf, 0xe4, 0x23, 0x5e, 0x0e, 0x82, 0x7a, 0xf0, 0x92, 0x74, 0xe9, 0xc6, 0x1d, 0x46,
	0x8c, 0x0b, 0xa3, 0x1b, 0xa1, 0x89, 0x37, 0x4c, 0x94, 0xfe, 0x00, 0x33, 0x96, 0xb1, 0x34, 0xb6,
	0x3d, 0x65, 0x66, 0x6c, 0xc2, 0x6f, 0x77, 0x63, 0xce, 0x4c, 0x4b, 0xab, 0xb0, 0x3b, 0xef, 0x33,
	0xef, 0x73, 0x18, 0xda, 0x02, 0xa4, 0x51, 0x6a, 0xfa, 0xb9, 0x22, 0x43, 0xd8, 0xe2, 0xd9, 0x83,
	0x9c, 0x28, 0x71, 0xc4, 0xdb, 0xd6, 0xaa, 0x28, 0xc7, 0x9e, 0x36, 0xa4, 0x44, 0x24, 0xdf, 0xe6,
	0x5f, 0x52, 0x2d, 0xaa, 0x73, 0x11, 0x96, 0xd5, 0xc1, 0xf7, 0x7f, 0xd8, 0x7c, 0x8e, 0x52, 0x13,
	0x14, 0x21, 0x9e, 0x42, 0xeb, 0x91, 0xe2, 0x0c, 0x3b, 0x7d, 0xbb, 0x9d, 0xe7, 0x89, 0x9c, 0x7b,
	0xdd, 0x66, 0xd4, 0xf9, 0xc9, 0x3f, 0xbc, 0x02, 0x78, 0x21, 0x4a, 0x46, 0x4a, 0x0a, 0x23, 0xb1,
	0xe7, 0xce, 0x6b, 0xc2, 0xd2, 0xc1, 0x2a, 0xb4, 0xea, 0x0d, 0xb4, 0x99, 0xf9, 0x52, 0x1b, 0x45,
	0x0b, 0x6c, 0xd4, 0x4a, 0xc4, 0xf2, 0xe1, 0x1a, 0x6a, 0xed, 0x73, 0xf7, 0xc3, 0x77, 0xd2, 0xdc,
	0x8e, 0xc6, 0xb8, 0xeb, 0x6a, 0x2e, 0xb1, 0xb7, 0xf7, 0x1b, 0x58, 0x65, 0x08, 0x1d, 0xce, 0xc6,
	0x88, 0x70, 0xf6, 0x90, 0x7d, 0x10, 0x1e, 0xd5, 0xa5, 0x25, 0x64, 0xf9, 0x78, 0x2d, 0xb7, 0x3b,
	0xae, 0xa1, 0x3b, 0x8c, 0xe9, 0x5e, 0x8a, 0xc4, 0xcc, 0x5e, 0xf9, 0x39, 0x22, 0xba, 0xf2, 0x92,
	0xf2, 0x82, 0xde, 0x0a, 0xb3, 0xf2, 0x00, 0xda, 0x41, 0x3a, 0x1d, 0xc7, 0xda, 0xf8, 0xb2, 0xd0,
	0xd5, 0xa5, 0x83, 0x74, 0xea, 0xcb, 0xa2, 0x71, 0xe9, 0x0a, 0x58, 0xe7, 0x12, 0x76, 0x4a, 0x87,
	0xff, 0xae, 0xc6, 0xba, 0xc3, 0x99, 0xad, 0xfd, 0x3f, 0xc4, 0x6a, 0x67, 0xb0, 0xf5, 0x14, 0x27,
	0xc9, 0x44, 0x64, 0x9f, 0x58, 0x16, 0xaa, 0xdc, 0x78, 0x91, 0xbe, 0x20, 0xed, 0x84, 0xf7, 0x0d,
	0xfb, 0x11, 0x5c, 0xfc, 0x04, 0x00, 0x00, 0xff, 0xff, 0x24, 0x62, 0xb6, 0xac, 0x4f, 0x02, 0x00,
	0x00,
}
