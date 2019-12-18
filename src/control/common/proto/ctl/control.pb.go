// Code generated by protoc-gen-go. DO NOT EDIT.
// source: control.proto

package ctl

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

// MgmtCtlClient is the client API for MgmtCtl service.
//
// For semantics around ctx use and closing/ending streaming RPCs, please refer to https://godoc.org/google.golang.org/grpc#ClientConn.NewStream.
type MgmtCtlClient interface {
	// Prepare nonvolatile storage devices for use with DAOS
	StoragePrepare(ctx context.Context, in *StoragePrepareReq, opts ...grpc.CallOption) (*StoragePrepareResp, error)
	// Retrieve details of nonvolatile storage on server, including health info
	StorageScan(ctx context.Context, in *StorageScanReq, opts ...grpc.CallOption) (*StorageScanResp, error)
	// Format nonvolatile storage devices for use with DAOS
	StorageFormat(ctx context.Context, in *StorageFormatReq, opts ...grpc.CallOption) (MgmtCtl_StorageFormatClient, error)
	// Query DAOS system membership (joined data-plane instances)
	SystemMemberQuery(ctx context.Context, in *SystemMemberQueryReq, opts ...grpc.CallOption) (*SystemMemberQueryResp, error)
	// Stop DAOS system (shutdown data-plane instances)
	SystemStop(ctx context.Context, in *SystemStopReq, opts ...grpc.CallOption) (*SystemStopResp, error)
	// Retrieve a list of supported fabric providers
	NetworkListProviders(ctx context.Context, in *ProviderListRequest, opts ...grpc.CallOption) (*ProviderListReply, error)
	// Perform a fabric scan to determine the available provider, device, NUMA node combinations
	NetworkScanDevices(ctx context.Context, in *DeviceScanRequest, opts ...grpc.CallOption) (MgmtCtl_NetworkScanDevicesClient, error)
}

type mgmtCtlClient struct {
	cc *grpc.ClientConn
}

func NewMgmtCtlClient(cc *grpc.ClientConn) MgmtCtlClient {
	return &mgmtCtlClient{cc}
}

func (c *mgmtCtlClient) StoragePrepare(ctx context.Context, in *StoragePrepareReq, opts ...grpc.CallOption) (*StoragePrepareResp, error) {
	out := new(StoragePrepareResp)
	err := c.cc.Invoke(ctx, "/ctl.MgmtCtl/StoragePrepare", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtCtlClient) StorageScan(ctx context.Context, in *StorageScanReq, opts ...grpc.CallOption) (*StorageScanResp, error) {
	out := new(StorageScanResp)
	err := c.cc.Invoke(ctx, "/ctl.MgmtCtl/StorageScan", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtCtlClient) StorageFormat(ctx context.Context, in *StorageFormatReq, opts ...grpc.CallOption) (MgmtCtl_StorageFormatClient, error) {
	stream, err := c.cc.NewStream(ctx, &_MgmtCtl_serviceDesc.Streams[0], "/ctl.MgmtCtl/StorageFormat", opts...)
	if err != nil {
		return nil, err
	}
	x := &mgmtCtlStorageFormatClient{stream}
	if err := x.ClientStream.SendMsg(in); err != nil {
		return nil, err
	}
	if err := x.ClientStream.CloseSend(); err != nil {
		return nil, err
	}
	return x, nil
}

type MgmtCtl_StorageFormatClient interface {
	Recv() (*StorageFormatResp, error)
	grpc.ClientStream
}

type mgmtCtlStorageFormatClient struct {
	grpc.ClientStream
}

func (x *mgmtCtlStorageFormatClient) Recv() (*StorageFormatResp, error) {
	m := new(StorageFormatResp)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

func (c *mgmtCtlClient) SystemMemberQuery(ctx context.Context, in *SystemMemberQueryReq, opts ...grpc.CallOption) (*SystemMemberQueryResp, error) {
	out := new(SystemMemberQueryResp)
	err := c.cc.Invoke(ctx, "/ctl.MgmtCtl/SystemMemberQuery", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtCtlClient) SystemStop(ctx context.Context, in *SystemStopReq, opts ...grpc.CallOption) (*SystemStopResp, error) {
	out := new(SystemStopResp)
	err := c.cc.Invoke(ctx, "/ctl.MgmtCtl/SystemStop", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtCtlClient) NetworkListProviders(ctx context.Context, in *ProviderListRequest, opts ...grpc.CallOption) (*ProviderListReply, error) {
	out := new(ProviderListReply)
	err := c.cc.Invoke(ctx, "/ctl.MgmtCtl/NetworkListProviders", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtCtlClient) NetworkScanDevices(ctx context.Context, in *DeviceScanRequest, opts ...grpc.CallOption) (MgmtCtl_NetworkScanDevicesClient, error) {
	stream, err := c.cc.NewStream(ctx, &_MgmtCtl_serviceDesc.Streams[1], "/ctl.MgmtCtl/NetworkScanDevices", opts...)
	if err != nil {
		return nil, err
	}
	x := &mgmtCtlNetworkScanDevicesClient{stream}
	if err := x.ClientStream.SendMsg(in); err != nil {
		return nil, err
	}
	if err := x.ClientStream.CloseSend(); err != nil {
		return nil, err
	}
	return x, nil
}

type MgmtCtl_NetworkScanDevicesClient interface {
	Recv() (*DeviceScanReply, error)
	grpc.ClientStream
}

type mgmtCtlNetworkScanDevicesClient struct {
	grpc.ClientStream
}

func (x *mgmtCtlNetworkScanDevicesClient) Recv() (*DeviceScanReply, error) {
	m := new(DeviceScanReply)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

// MgmtCtlServer is the server API for MgmtCtl service.
type MgmtCtlServer interface {
	// Prepare nonvolatile storage devices for use with DAOS
	StoragePrepare(context.Context, *StoragePrepareReq) (*StoragePrepareResp, error)
	// Retrieve details of nonvolatile storage on server, including health info
	StorageScan(context.Context, *StorageScanReq) (*StorageScanResp, error)
	// Format nonvolatile storage devices for use with DAOS
	StorageFormat(*StorageFormatReq, MgmtCtl_StorageFormatServer) error
	// Query DAOS system membership (joined data-plane instances)
	SystemMemberQuery(context.Context, *SystemMemberQueryReq) (*SystemMemberQueryResp, error)
	// Stop DAOS system (shutdown data-plane instances)
	SystemStop(context.Context, *SystemStopReq) (*SystemStopResp, error)
	// Retrieve a list of supported fabric providers
	NetworkListProviders(context.Context, *ProviderListRequest) (*ProviderListReply, error)
	// Perform a fabric scan to determine the available provider, device, NUMA node combinations
	NetworkScanDevices(*DeviceScanRequest, MgmtCtl_NetworkScanDevicesServer) error
}

func RegisterMgmtCtlServer(s *grpc.Server, srv MgmtCtlServer) {
	s.RegisterService(&_MgmtCtl_serviceDesc, srv)
}

func _MgmtCtl_StoragePrepare_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(StoragePrepareReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtCtlServer).StoragePrepare(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/ctl.MgmtCtl/StoragePrepare",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtCtlServer).StoragePrepare(ctx, req.(*StoragePrepareReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtCtl_StorageScan_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(StorageScanReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtCtlServer).StorageScan(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/ctl.MgmtCtl/StorageScan",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtCtlServer).StorageScan(ctx, req.(*StorageScanReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtCtl_StorageFormat_Handler(srv interface{}, stream grpc.ServerStream) error {
	m := new(StorageFormatReq)
	if err := stream.RecvMsg(m); err != nil {
		return err
	}
	return srv.(MgmtCtlServer).StorageFormat(m, &mgmtCtlStorageFormatServer{stream})
}

type MgmtCtl_StorageFormatServer interface {
	Send(*StorageFormatResp) error
	grpc.ServerStream
}

type mgmtCtlStorageFormatServer struct {
	grpc.ServerStream
}

func (x *mgmtCtlStorageFormatServer) Send(m *StorageFormatResp) error {
	return x.ServerStream.SendMsg(m)
}

func _MgmtCtl_SystemMemberQuery_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(SystemMemberQueryReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtCtlServer).SystemMemberQuery(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/ctl.MgmtCtl/SystemMemberQuery",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtCtlServer).SystemMemberQuery(ctx, req.(*SystemMemberQueryReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtCtl_SystemStop_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(SystemStopReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtCtlServer).SystemStop(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/ctl.MgmtCtl/SystemStop",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtCtlServer).SystemStop(ctx, req.(*SystemStopReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtCtl_NetworkListProviders_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(ProviderListRequest)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtCtlServer).NetworkListProviders(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/ctl.MgmtCtl/NetworkListProviders",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtCtlServer).NetworkListProviders(ctx, req.(*ProviderListRequest))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtCtl_NetworkScanDevices_Handler(srv interface{}, stream grpc.ServerStream) error {
	m := new(DeviceScanRequest)
	if err := stream.RecvMsg(m); err != nil {
		return err
	}
	return srv.(MgmtCtlServer).NetworkScanDevices(m, &mgmtCtlNetworkScanDevicesServer{stream})
}

type MgmtCtl_NetworkScanDevicesServer interface {
	Send(*DeviceScanReply) error
	grpc.ServerStream
}

type mgmtCtlNetworkScanDevicesServer struct {
	grpc.ServerStream
}

func (x *mgmtCtlNetworkScanDevicesServer) Send(m *DeviceScanReply) error {
	return x.ServerStream.SendMsg(m)
}

var _MgmtCtl_serviceDesc = grpc.ServiceDesc{
	ServiceName: "ctl.MgmtCtl",
	HandlerType: (*MgmtCtlServer)(nil),
	Methods: []grpc.MethodDesc{
		{
			MethodName: "StoragePrepare",
			Handler:    _MgmtCtl_StoragePrepare_Handler,
		},
		{
			MethodName: "StorageScan",
			Handler:    _MgmtCtl_StorageScan_Handler,
		},
		{
			MethodName: "SystemMemberQuery",
			Handler:    _MgmtCtl_SystemMemberQuery_Handler,
		},
		{
			MethodName: "SystemStop",
			Handler:    _MgmtCtl_SystemStop_Handler,
		},
		{
			MethodName: "NetworkListProviders",
			Handler:    _MgmtCtl_NetworkListProviders_Handler,
		},
	},
	Streams: []grpc.StreamDesc{
		{
			StreamName:    "StorageFormat",
			Handler:       _MgmtCtl_StorageFormat_Handler,
			ServerStreams: true,
		},
		{
			StreamName:    "NetworkScanDevices",
			Handler:       _MgmtCtl_NetworkScanDevices_Handler,
			ServerStreams: true,
		},
	},
	Metadata: "control.proto",
}

func init() { proto.RegisterFile("control.proto", fileDescriptor_control_67afdb7a110c1948) }

var fileDescriptor_control_67afdb7a110c1948 = []byte{
	// 288 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x74, 0x91, 0xcd, 0x4a, 0xc3, 0x40,
	0x10, 0xc7, 0x5b, 0x0a, 0x0a, 0xab, 0x11, 0x9c, 0xc6, 0xaa, 0x39, 0xf6, 0x01, 0x42, 0xd1, 0x83,
	0xe0, 0xd1, 0x4a, 0x0f, 0xd2, 0x4a, 0x6d, 0x1e, 0x40, 0xd2, 0x75, 0x28, 0xc1, 0x24, 0xbb, 0x9d,
	0x9d, 0x56, 0xf2, 0xc8, 0xbe, 0x85, 0xec, 0x87, 0x90, 0x9a, 0x7a, 0x9c, 0xdf, 0xff, 0x83, 0xdd,
	0x19, 0x11, 0x49, 0x55, 0x33, 0xa9, 0x32, 0xd5, 0xa4, 0x58, 0xc1, 0x40, 0x72, 0x99, 0x44, 0x86,
	0x15, 0xe5, 0x1b, 0xf4, 0x2c, 0x81, 0x1a, 0xf9, 0x4b, 0xd1, 0xe7, 0xbb, 0x91, 0x79, 0x1d, 0xd8,
	0xb9, 0x69, 0x0c, 0x63, 0xe5, 0xa7, 0xbb, 0xef, 0x81, 0x38, 0x5d, 0x6c, 0x2a, 0x9e, 0x72, 0x09,
	0x53, 0x71, 0x91, 0xf9, 0xf8, 0x92, 0x50, 0xe7, 0x84, 0x30, 0x4a, 0x25, 0x97, 0xe9, 0x21, 0x5c,
	0xe1, 0x36, 0xb9, 0x3e, 0xca, 0x8d, 0x1e, 0xf7, 0xe0, 0x51, 0x9c, 0x05, 0x9e, 0xc9, 0xbc, 0x86,
	0x61, 0xdb, 0x69, 0x89, 0x8d, 0xc7, 0x5d, 0xe8, 0xb2, 0x4f, 0x22, 0x0a, 0x70, 0xa6, 0xa8, 0xca,
	0x19, 0xae, 0xda, 0x46, 0xcf, 0x6c, 0x7e, 0x74, 0x0c, 0xdb, 0x86, 0x49, 0x1f, 0xe6, 0xe2, 0x32,
	0x73, 0x1f, 0x5c, 0x60, 0xb5, 0x46, 0x7a, 0xdb, 0x21, 0x35, 0x70, 0xeb, 0x03, 0x7f, 0xb9, 0xed,
	0x4a, 0xfe, 0x93, 0xdc, 0x8b, 0x1e, 0x84, 0xf0, 0x52, 0xc6, 0x4a, 0x03, 0xb4, 0xbc, 0x16, 0xd8,
	0xfc, 0xb0, 0xc3, 0x5c, 0xf0, 0x45, 0xc4, 0xaf, 0x7e, 0xf7, 0xf3, 0xc2, 0xf0, 0x92, 0xd4, 0xbe,
	0xf8, 0x40, 0x32, 0x70, 0xe3, 0xec, 0xbf, 0xb3, 0xd5, 0x56, 0xb8, 0xdd, 0xa1, 0xe1, 0xf0, 0xa9,
	0x43, 0x45, 0x97, 0xcd, 0xb8, 0x07, 0x33, 0x01, 0xa1, 0xcb, 0xee, 0xea, 0x19, 0xf7, 0x85, 0x44,
	0x13, 0x6e, 0xe3, 0xa7, 0xb0, 0x58, 0xd7, 0x13, 0x77, 0xb8, 0x6b, 0x99, 0xf4, 0xd7, 0x27, 0xee,
	0xe4, 0xf7, 0x3f, 0x01, 0x00, 0x00, 0xff, 0xff, 0x4d, 0x93, 0x6e, 0xc9, 0x39, 0x02, 0x00, 0x00,
}
