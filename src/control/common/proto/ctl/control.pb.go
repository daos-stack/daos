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
	// Update nonvolatile storage device firmware
	StorageUpdate(ctx context.Context, in *StorageUpdateReq, opts ...grpc.CallOption) (MgmtCtl_StorageUpdateClient, error)
	// Perform burn-in testing to verify nonvolatile storage devices
	StorageBurnIn(ctx context.Context, in *StorageBurnInReq, opts ...grpc.CallOption) (MgmtCtl_StorageBurnInClient, error)
	// Fetch FIO configuration file specifying burn-in jobs/workloads
	FetchFioConfigPaths(ctx context.Context, in *EmptyReq, opts ...grpc.CallOption) (MgmtCtl_FetchFioConfigPathsClient, error)
	// List features supported on remote storage server/DAOS system
	ListFeatures(ctx context.Context, in *EmptyReq, opts ...grpc.CallOption) (MgmtCtl_ListFeaturesClient, error)
	// Query DAOS system membership (joined data-plane instances)
	SystemMemberQuery(ctx context.Context, in *SystemMemberQueryReq, opts ...grpc.CallOption) (*SystemMemberQueryResp, error)
	// Stop DAOS system (shutdown data-plane instances)
	SystemStop(ctx context.Context, in *SystemStopReq, opts ...grpc.CallOption) (*SystemStopResp, error)
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

func (c *mgmtCtlClient) StorageUpdate(ctx context.Context, in *StorageUpdateReq, opts ...grpc.CallOption) (MgmtCtl_StorageUpdateClient, error) {
	stream, err := c.cc.NewStream(ctx, &_MgmtCtl_serviceDesc.Streams[1], "/ctl.MgmtCtl/StorageUpdate", opts...)
	if err != nil {
		return nil, err
	}
	x := &mgmtCtlStorageUpdateClient{stream}
	if err := x.ClientStream.SendMsg(in); err != nil {
		return nil, err
	}
	if err := x.ClientStream.CloseSend(); err != nil {
		return nil, err
	}
	return x, nil
}

type MgmtCtl_StorageUpdateClient interface {
	Recv() (*StorageUpdateResp, error)
	grpc.ClientStream
}

type mgmtCtlStorageUpdateClient struct {
	grpc.ClientStream
}

func (x *mgmtCtlStorageUpdateClient) Recv() (*StorageUpdateResp, error) {
	m := new(StorageUpdateResp)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

func (c *mgmtCtlClient) StorageBurnIn(ctx context.Context, in *StorageBurnInReq, opts ...grpc.CallOption) (MgmtCtl_StorageBurnInClient, error) {
	stream, err := c.cc.NewStream(ctx, &_MgmtCtl_serviceDesc.Streams[2], "/ctl.MgmtCtl/StorageBurnIn", opts...)
	if err != nil {
		return nil, err
	}
	x := &mgmtCtlStorageBurnInClient{stream}
	if err := x.ClientStream.SendMsg(in); err != nil {
		return nil, err
	}
	if err := x.ClientStream.CloseSend(); err != nil {
		return nil, err
	}
	return x, nil
}

type MgmtCtl_StorageBurnInClient interface {
	Recv() (*StorageBurnInResp, error)
	grpc.ClientStream
}

type mgmtCtlStorageBurnInClient struct {
	grpc.ClientStream
}

func (x *mgmtCtlStorageBurnInClient) Recv() (*StorageBurnInResp, error) {
	m := new(StorageBurnInResp)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

func (c *mgmtCtlClient) FetchFioConfigPaths(ctx context.Context, in *EmptyReq, opts ...grpc.CallOption) (MgmtCtl_FetchFioConfigPathsClient, error) {
	stream, err := c.cc.NewStream(ctx, &_MgmtCtl_serviceDesc.Streams[3], "/ctl.MgmtCtl/FetchFioConfigPaths", opts...)
	if err != nil {
		return nil, err
	}
	x := &mgmtCtlFetchFioConfigPathsClient{stream}
	if err := x.ClientStream.SendMsg(in); err != nil {
		return nil, err
	}
	if err := x.ClientStream.CloseSend(); err != nil {
		return nil, err
	}
	return x, nil
}

type MgmtCtl_FetchFioConfigPathsClient interface {
	Recv() (*FilePath, error)
	grpc.ClientStream
}

type mgmtCtlFetchFioConfigPathsClient struct {
	grpc.ClientStream
}

func (x *mgmtCtlFetchFioConfigPathsClient) Recv() (*FilePath, error) {
	m := new(FilePath)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

func (c *mgmtCtlClient) ListFeatures(ctx context.Context, in *EmptyReq, opts ...grpc.CallOption) (MgmtCtl_ListFeaturesClient, error) {
	stream, err := c.cc.NewStream(ctx, &_MgmtCtl_serviceDesc.Streams[4], "/ctl.MgmtCtl/ListFeatures", opts...)
	if err != nil {
		return nil, err
	}
	x := &mgmtCtlListFeaturesClient{stream}
	if err := x.ClientStream.SendMsg(in); err != nil {
		return nil, err
	}
	if err := x.ClientStream.CloseSend(); err != nil {
		return nil, err
	}
	return x, nil
}

type MgmtCtl_ListFeaturesClient interface {
	Recv() (*Feature, error)
	grpc.ClientStream
}

type mgmtCtlListFeaturesClient struct {
	grpc.ClientStream
}

func (x *mgmtCtlListFeaturesClient) Recv() (*Feature, error) {
	m := new(Feature)
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

// MgmtCtlServer is the server API for MgmtCtl service.
type MgmtCtlServer interface {
	// Prepare nonvolatile storage devices for use with DAOS
	StoragePrepare(context.Context, *StoragePrepareReq) (*StoragePrepareResp, error)
	// Retrieve details of nonvolatile storage on server, including health info
	StorageScan(context.Context, *StorageScanReq) (*StorageScanResp, error)
	// Format nonvolatile storage devices for use with DAOS
	StorageFormat(*StorageFormatReq, MgmtCtl_StorageFormatServer) error
	// Update nonvolatile storage device firmware
	StorageUpdate(*StorageUpdateReq, MgmtCtl_StorageUpdateServer) error
	// Perform burn-in testing to verify nonvolatile storage devices
	StorageBurnIn(*StorageBurnInReq, MgmtCtl_StorageBurnInServer) error
	// Fetch FIO configuration file specifying burn-in jobs/workloads
	FetchFioConfigPaths(*EmptyReq, MgmtCtl_FetchFioConfigPathsServer) error
	// List features supported on remote storage server/DAOS system
	ListFeatures(*EmptyReq, MgmtCtl_ListFeaturesServer) error
	// Query DAOS system membership (joined data-plane instances)
	SystemMemberQuery(context.Context, *SystemMemberQueryReq) (*SystemMemberQueryResp, error)
	// Stop DAOS system (shutdown data-plane instances)
	SystemStop(context.Context, *SystemStopReq) (*SystemStopResp, error)
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

func _MgmtCtl_StorageUpdate_Handler(srv interface{}, stream grpc.ServerStream) error {
	m := new(StorageUpdateReq)
	if err := stream.RecvMsg(m); err != nil {
		return err
	}
	return srv.(MgmtCtlServer).StorageUpdate(m, &mgmtCtlStorageUpdateServer{stream})
}

type MgmtCtl_StorageUpdateServer interface {
	Send(*StorageUpdateResp) error
	grpc.ServerStream
}

type mgmtCtlStorageUpdateServer struct {
	grpc.ServerStream
}

func (x *mgmtCtlStorageUpdateServer) Send(m *StorageUpdateResp) error {
	return x.ServerStream.SendMsg(m)
}

func _MgmtCtl_StorageBurnIn_Handler(srv interface{}, stream grpc.ServerStream) error {
	m := new(StorageBurnInReq)
	if err := stream.RecvMsg(m); err != nil {
		return err
	}
	return srv.(MgmtCtlServer).StorageBurnIn(m, &mgmtCtlStorageBurnInServer{stream})
}

type MgmtCtl_StorageBurnInServer interface {
	Send(*StorageBurnInResp) error
	grpc.ServerStream
}

type mgmtCtlStorageBurnInServer struct {
	grpc.ServerStream
}

func (x *mgmtCtlStorageBurnInServer) Send(m *StorageBurnInResp) error {
	return x.ServerStream.SendMsg(m)
}

func _MgmtCtl_FetchFioConfigPaths_Handler(srv interface{}, stream grpc.ServerStream) error {
	m := new(EmptyReq)
	if err := stream.RecvMsg(m); err != nil {
		return err
	}
	return srv.(MgmtCtlServer).FetchFioConfigPaths(m, &mgmtCtlFetchFioConfigPathsServer{stream})
}

type MgmtCtl_FetchFioConfigPathsServer interface {
	Send(*FilePath) error
	grpc.ServerStream
}

type mgmtCtlFetchFioConfigPathsServer struct {
	grpc.ServerStream
}

func (x *mgmtCtlFetchFioConfigPathsServer) Send(m *FilePath) error {
	return x.ServerStream.SendMsg(m)
}

func _MgmtCtl_ListFeatures_Handler(srv interface{}, stream grpc.ServerStream) error {
	m := new(EmptyReq)
	if err := stream.RecvMsg(m); err != nil {
		return err
	}
	return srv.(MgmtCtlServer).ListFeatures(m, &mgmtCtlListFeaturesServer{stream})
}

type MgmtCtl_ListFeaturesServer interface {
	Send(*Feature) error
	grpc.ServerStream
}

type mgmtCtlListFeaturesServer struct {
	grpc.ServerStream
}

func (x *mgmtCtlListFeaturesServer) Send(m *Feature) error {
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
	},
	Streams: []grpc.StreamDesc{
		{
			StreamName:    "StorageFormat",
			Handler:       _MgmtCtl_StorageFormat_Handler,
			ServerStreams: true,
		},
		{
			StreamName:    "StorageUpdate",
			Handler:       _MgmtCtl_StorageUpdate_Handler,
			ServerStreams: true,
		},
		{
			StreamName:    "StorageBurnIn",
			Handler:       _MgmtCtl_StorageBurnIn_Handler,
			ServerStreams: true,
		},
		{
			StreamName:    "FetchFioConfigPaths",
			Handler:       _MgmtCtl_FetchFioConfigPaths_Handler,
			ServerStreams: true,
		},
		{
			StreamName:    "ListFeatures",
			Handler:       _MgmtCtl_ListFeatures_Handler,
			ServerStreams: true,
		},
	},
	Metadata: "control.proto",
}

func init() { proto.RegisterFile("control.proto", fileDescriptor_control_18261ed8928eea5c) }

var fileDescriptor_control_18261ed8928eea5c = []byte{
	// 307 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x74, 0x92, 0xcd, 0x4a, 0xc3, 0x40,
	0x10, 0x80, 0x15, 0x45, 0x61, 0x4d, 0x0a, 0x6e, 0xb5, 0x62, 0x8e, 0x3e, 0x40, 0x15, 0x3d, 0x14,
	0x3c, 0xb6, 0x18, 0x10, 0x5a, 0xa8, 0x06, 0x1f, 0x60, 0xbb, 0x4e, 0xd3, 0x40, 0xf6, 0xc7, 0xdd,
	0xc9, 0x21, 0x4f, 0xe8, 0x6b, 0xc9, 0xfe, 0x14, 0x92, 0xa6, 0x1e, 0xe7, 0x9b, 0xfd, 0x3e, 0x02,
	0x13, 0x92, 0x72, 0x25, 0xd1, 0xa8, 0x7a, 0xaa, 0x8d, 0x42, 0x45, 0xcf, 0x38, 0xd6, 0x59, 0xc2,
	0x95, 0x10, 0x4a, 0x06, 0x94, 0xa5, 0x16, 0x95, 0x61, 0x25, 0xc4, 0x71, 0xb4, 0x05, 0x86, 0x8d,
	0x01, 0x1b, 0xe7, 0xc4, 0xb6, 0x16, 0x41, 0x84, 0xe9, 0xf9, 0xf7, 0x9c, 0x5c, 0xae, 0x4a, 0x81,
	0x0b, 0xac, 0xe9, 0x82, 0x8c, 0x8a, 0xa0, 0xae, 0x0d, 0x68, 0x66, 0x80, 0x4e, 0xa6, 0x1c, 0xeb,
	0x69, 0x1f, 0x7e, 0xc2, 0x4f, 0x76, 0x77, 0x94, 0x5b, 0xfd, 0x70, 0x42, 0x5f, 0xc9, 0x55, 0xe4,
	0x05, 0x67, 0x92, 0x8e, 0xbb, 0x2f, 0x1d, 0x71, 0xfa, 0xcd, 0x10, 0x7a, 0x77, 0x4e, 0xd2, 0x08,
	0x73, 0x65, 0x04, 0x43, 0x7a, 0xdb, 0x7d, 0x18, 0x98, 0xf3, 0x27, 0xc7, 0xb0, 0x2b, 0x3c, 0x9d,
	0x76, 0x1a, 0x5f, 0xfa, 0x9b, 0x21, 0xf4, 0x1b, 0x81, 0x0d, 0x1a, 0x7b, 0x3c, 0x68, 0xcc, 0x1b,
	0x23, 0xdf, 0x65, 0xbf, 0x11, 0xd8, 0xa0, 0xb1, 0xc7, 0xb1, 0x31, 0x23, 0xe3, 0x1c, 0x90, 0xef,
	0xf2, 0x4a, 0x2d, 0x94, 0xdc, 0x56, 0xe5, 0x9a, 0xe1, 0xce, 0xd2, 0xd4, 0x2b, 0x6f, 0x42, 0x63,
	0xeb, 0x0a, 0x61, 0xcc, 0xab, 0x1a, 0xdc, 0xda, 0x8b, 0x8f, 0x24, 0x59, 0x56, 0x16, 0xf3, 0x78,
	0xb5, 0x43, 0x23, 0x09, 0x46, 0xd8, 0x7a, 0x61, 0x49, 0xae, 0x0b, 0x7f, 0xd2, 0x15, 0x88, 0x0d,
	0x98, 0x8f, 0x06, 0x4c, 0x4b, 0xef, 0xc3, 0xa7, 0x1d, 0x72, 0x57, 0xc8, 0xfe, 0x5b, 0xf9, 0x1b,
	0xcc, 0x08, 0x09, 0xab, 0x02, 0x95, 0xa6, 0xb4, 0xf3, 0xd6, 0x01, 0xe7, 0x8f, 0x07, 0xcc, 0x89,
	0x9b, 0x0b, 0xff, 0x43, 0xbd, 0xfc, 0x05, 0x00, 0x00, 0xff, 0xff, 0xc8, 0xc2, 0xb7, 0x5f, 0xa1,
	0x02, 0x00, 0x00,
}
