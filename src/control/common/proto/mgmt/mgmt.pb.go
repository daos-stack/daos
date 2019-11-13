// Code generated by protoc-gen-go. DO NOT EDIT.
// source: mgmt.proto

package mgmt

import (
	context "context"
	fmt "fmt"
	proto "github.com/golang/protobuf/proto"
	grpc "google.golang.org/grpc"
	codes "google.golang.org/grpc/codes"
	status "google.golang.org/grpc/status"
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

func init() { proto.RegisterFile("mgmt.proto", fileDescriptor_24cf82780fd24e73) }

var fileDescriptor_24cf82780fd24e73 = []byte{
	// 316 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x6c, 0x92, 0xcd, 0x4e, 0x02, 0x31,
	0x10, 0xc7, 0x3d, 0x10, 0x95, 0x41, 0x50, 0x07, 0x3f, 0x12, 0x8e, 0x5e, 0xbc, 0x61, 0xc4, 0x68,
	0x62, 0xf4, 0x22, 0x6c, 0xe2, 0x17, 0x26, 0x0a, 0x0f, 0x60, 0x2a, 0x54, 0xd8, 0xb8, 0xcb, 0x40,
	0x5b, 0x37, 0xe1, 0xb9, 0x7c, 0x41, 0x33, 0x6d, 0x77, 0xb7, 0x0a, 0xb7, 0x99, 0x5f, 0xff, 0xbf,
	0x69, 0x07, 0x16, 0x20, 0x9d, 0xa4, 0xa6, 0x3d, 0x57, 0x64, 0x08, 0x2b, 0x5c, 0xb7, 0x60, 0x4e,
	0x94, 0x38, 0xd2, 0xaa, 0x6a, 0x95, 0xf9, 0xb2, 0xa9, 0x0d, 0x29, 0x31, 0x91, 0xef, 0x8b, 0x6f,
	0xa9, 0x96, 0xf9, 0xb9, 0x18, 0xf9, 0x68, 0xe7, 0xa7, 0x02, 0x5b, 0x2f, 0x93, 0xd4, 0x0c, 0xb3,
	0x11, 0x9e, 0x42, 0xe5, 0x89, 0xe2, 0x19, 0xd6, 0xdb, 0x76, 0x3a, 0xd7, 0x03, 0xb9, 0x68, 0x35,
	0xc2, 0x56, 0xcf, 0x4f, 0x36, 0xf0, 0x1a, 0xe0, 0x95, 0x28, 0xe9, 0x29, 0x29, 0x8c, 0xc4, 0xa6,
	0x3b, 0x2f, 0x09, 0x4b, 0x07, 0xab, 0xd0, 0xaa, 0xb7, 0x50, 0x63, 0x16, 0x49, 0x6d, 0x14, 0x2d,
	0x31, 0x88, 0x79, 0xc4, 0xf2, 0xe1, 0x1a, 0x6a, 0xed, 0x73, 0x77, 0xf1, 0xbd, 0x34, 0x77, 0xbd,
	0x3e, 0xee, 0xba, 0x98, 0xeb, 0xd8, 0xdb, 0xfb, 0x0b, 0xac, 0xd2, 0x85, 0x3a, 0xf7, 0xc6, 0x88,
	0xd1, 0xf4, 0x71, 0xf6, 0x49, 0x78, 0x54, 0x86, 0x0a, 0xc8, 0xf2, 0xf1, 0x5a, 0x6e, 0x67, 0xdc,
	0x40, 0xa3, 0x1b, 0xd3, 0x83, 0x14, 0x89, 0x99, 0xbe, 0xf1, 0xef, 0x88, 0xe8, 0xc2, 0x05, 0xe5,
	0x01, 0xcd, 0x15, 0x66, 0xe5, 0x0e, 0xd4, 0x86, 0xe9, 0xb8, 0x1f, 0x6b, 0x13, 0xc9, 0x4c, 0xe7,
	0x8f, 0x1e, 0xa6, 0xe3, 0x48, 0x66, 0xc1, 0xa3, 0x73, 0x60, 0x9d, 0x4b, 0xd8, 0xf1, 0x0e, 0xaf,
	0xab, 0xb1, 0xcc, 0x70, 0xcf, 0xd6, 0xfe, 0x3f, 0x62, 0xb5, 0x33, 0xd8, 0x7e, 0x8e, 0x93, 0x64,
	0x20, 0x66, 0x5f, 0xe8, 0x03, 0x79, 0x1f, 0xfc, 0x91, 0x91, 0x20, 0xed, 0x85, 0x2b, 0xa8, 0x96,
	0x97, 0xf8, 0x9d, 0x0a, 0x10, 0xec, 0x14, 0x30, 0xf6, 0x3e, 0x36, 0xed, 0xc7, 0x73, 0xf1, 0x1b,
	0x00, 0x00, 0xff, 0xff, 0x35, 0xc8, 0xae, 0xc3, 0x87, 0x02, 0x00, 0x00,
}

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
	// List all pools in a DAOS system: basic info: UUIDs, service ranks
	ListPools(ctx context.Context, in *ListPoolsReq, opts ...grpc.CallOption) (*ListPoolsResp, error)
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

func (c *mgmtSvcClient) ListPools(ctx context.Context, in *ListPoolsReq, opts ...grpc.CallOption) (*ListPoolsResp, error) {
	out := new(ListPoolsResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/ListPools", in, out, opts...)
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
	// List all pools in a DAOS system: basic info: UUIDs, service ranks
	ListPools(context.Context, *ListPoolsReq) (*ListPoolsResp, error)
}

// UnimplementedMgmtSvcServer can be embedded to have forward compatible implementations.
type UnimplementedMgmtSvcServer struct {
}

func (*UnimplementedMgmtSvcServer) Join(ctx context.Context, req *JoinReq) (*JoinResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method Join not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolCreate(ctx context.Context, req *PoolCreateReq) (*PoolCreateResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolCreate not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolDestroy(ctx context.Context, req *PoolDestroyReq) (*PoolDestroyResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolDestroy not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolGetACL(ctx context.Context, req *GetACLReq) (*GetACLResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolGetACL not implemented")
}
func (*UnimplementedMgmtSvcServer) GetAttachInfo(ctx context.Context, req *GetAttachInfoReq) (*GetAttachInfoResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method GetAttachInfo not implemented")
}
func (*UnimplementedMgmtSvcServer) BioHealthQuery(ctx context.Context, req *BioHealthReq) (*BioHealthResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method BioHealthQuery not implemented")
}
func (*UnimplementedMgmtSvcServer) SmdListDevs(ctx context.Context, req *SmdDevReq) (*SmdDevResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method SmdListDevs not implemented")
}
func (*UnimplementedMgmtSvcServer) SmdListPools(ctx context.Context, req *SmdPoolReq) (*SmdPoolResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method SmdListPools not implemented")
}
func (*UnimplementedMgmtSvcServer) KillRank(ctx context.Context, req *KillRankReq) (*DaosResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method KillRank not implemented")
}
func (*UnimplementedMgmtSvcServer) ListPools(ctx context.Context, req *ListPoolsReq) (*ListPoolsResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method ListPools not implemented")
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

func _MgmtSvc_ListPools_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(ListPoolsReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).ListPools(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/ListPools",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).ListPools(ctx, req.(*ListPoolsReq))
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
		{
			MethodName: "ListPools",
			Handler:    _MgmtSvc_ListPools_Handler,
		},
	},
	Streams:  []grpc.StreamDesc{},
	Metadata: "mgmt.proto",
}
