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
	// 444 bytes of a gzipped FileDescriptorProto
	0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x94, 0x94, 0x4b, 0x6f, 0xd3, 0x40,
	0x10, 0x80, 0x39, 0x54, 0x94, 0x4e, 0x9b, 0x90, 0x6e, 0x0a, 0x48, 0x39, 0x72, 0xe1, 0x16, 0xa4,
	0xf2, 0xac, 0xe0, 0x42, 0x63, 0xf1, 0x4c, 0x45, 0xa9, 0xc5, 0x19, 0x2d, 0xf1, 0x34, 0xb1, 0xb0,
	0xbd, 0xee, 0xee, 0xc4, 0x55, 0xff, 0x3c, 0x42, 0xb3, 0x0f, 0x7b, 0xd3, 0x9a, 0x43, 0x6e, 0x3b,
	0x5f, 0xe6, 0x9b, 0xd9, 0x9d, 0x49, 0x02, 0x50, 0x2e, 0x4b, 0x9a, 0xd6, 0x5a, 0x91, 0x12, 0x3b,
	0x7c, 0x9e, 0x40, 0xad, 0x54, 0xe1, 0xc8, 0x64, 0xcf, 0xe8, 0xc6, 0x1f, 0xc7, 0x86, 0x94, 0x96,
	0x4b, 0xfc, 0x75, 0xb5, 0x46, 0x7d, 0x13, 0x3e, 0x97, 0x0b, 0x9f, 0x7a, 0xfc, 0x77, 0x17, 0x76,
	0xcf, 0x96, 0x25, 0xa5, 0xcd, 0x42, 0x3c, 0x83, 0x9d, 0xaf, 0x2a, 0xaf, 0xc4, 0x60, 0x6a, 0xab,
	0xf3, 0xf9, 0x02, 0xaf, 0x26, 0xc3, 0x38, 0x34, 0xf5, 0xd3, 0x7b, 0xe2, 0x3d, 0xec, 0xcf, 0x51,
	0x66, 0xa8, 0x7f, 0x70, 0x51, 0x71, 0xe4, 0x12, 0x22, 0xc4, 0xda, 0xa3, 0x1e, 0x6a, 0xed, 0x13,
	0x80, 0x73, 0xa5, 0x8a, 0x99, 0x46, 0x49, 0x28, 0xc6, 0x2e, 0xad, 0x23, 0xec, 0x1e, 0xdd, 0x85,
	0xa1, 0x31, 0xb3, 0x04, 0x0d, 0x69, 0xd5, 0x36, 0x8e, 0x50, 0xd4, 0x78, 0x83, 0x5a, 0x7b, 0xea,
	0x1a, 0x7f, 0x42, 0xfa, 0x30, 0x9b, 0x8b, 0x87, 0x2e, 0xcd, 0x45, 0xec, 0xf9, 0x67, 0xdb, 0xc8,
	0xe6, 0xbf, 0x81, 0x11, 0xe7, 0x7f, 0x6f, 0x50, 0x5f, 0xeb, 0x9c, 0x90, 0x2d, 0xe1, 0x92, 0xce,
	0x54, 0x96, 0x5f, 0xde, 0xfc, 0x4f, 0x7c, 0x09, 0x03, 0x16, 0x7f, 0xd6, 0x99, 0xdc, 0xde, 0x4a,
	0xb0, 0xc0, 0x0d, 0xab, 0x05, 0xbd, 0xd6, 0x29, 0x0c, 0xf8, 0x09, 0x44, 0x72, 0xb1, 0xfa, 0x52,
	0x5d, 0x2a, 0xf1, 0xb8, 0x7b, 0x57, 0x0b, 0xd9, 0x7c, 0xd2, 0xcb, 0x6d, 0x8d, 0x77, 0x30, 0x3c,
	0xcd, 0xd5, 0x67, 0x94, 0x05, 0xad, 0xdc, 0x4a, 0x7d, 0xeb, 0x96, 0x72, 0x81, 0xf1, 0x1d, 0x66,
	0xe5, 0x63, 0xd8, 0x4f, 0xcb, 0x6c, 0x9e, 0x1b, 0x4a, 0xb0, 0x31, 0x61, 0xac, 0x69, 0x99, 0x25,
	0xd8, 0xb0, 0x36, 0xda, 0x04, 0xd6, 0x79, 0x05, 0x07, 0xde, 0xe1, 0x17, 0x1b, 0xd1, 0xe5, 0x70,
	0xcc, 0xd6, 0xe1, 0x2d, 0xe2, 0x17, 0x72, 0x70, 0xae, 0xb1, 0x4e, 0x57, 0x6b, 0xca, 0xd4, 0x75,
	0x25, 0xc2, 0xa6, 0x23, 0x16, 0x7d, 0x61, 0x13, 0xa9, 0x8c, 0x17, 0x9f, 0xc3, 0x83, 0x6f, 0x79,
	0x51, 0x5c, 0xc8, 0xea, 0x8f, 0xf0, 0x95, 0x43, 0xdc, 0x2f, 0xbc, 0x86, 0xbd, 0xee, 0x76, 0x7e,
	0x18, 0x2d, 0x88, 0x86, 0x11, 0x31, 0xeb, 0xbd, 0x85, 0x41, 0x82, 0x4d, 0x4a, 0x92, 0xd0, 0x0d,
	0xf2, 0x30, 0xec, 0xd0, 0x41, 0x56, 0xc5, 0x6d, 0xe4, 0x77, 0x30, 0x4a, 0xdd, 0x4f, 0x35, 0x45,
	0xfa, 0x28, 0xd7, 0x05, 0x6d, 0x21, 0x9f, 0xc0, 0x90, 0x6f, 0x32, 0x53, 0x15, 0xc9, 0xbc, 0x42,
	0x6d, 0x82, 0x1a, 0x68, 0xa4, 0x76, 0x88, 0xd5, 0xdf, 0xf7, 0xed, 0xff, 0xc0, 0x8b, 0x7f, 0x01,
	0x00, 0x00, 0xff, 0xff, 0xf1, 0x64, 0x4d, 0x9f, 0x52, 0x04, 0x00, 0x00,
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
	// LeaderQuery provides a mechanism for clients to discover
	// the system's current Management Service leader
	LeaderQuery(ctx context.Context, in *LeaderQueryReq, opts ...grpc.CallOption) (*LeaderQueryResp, error)
	// Create a DAOS pool allocated across a number of ranks
	PoolCreate(ctx context.Context, in *PoolCreateReq, opts ...grpc.CallOption) (*PoolCreateResp, error)
	// Destroy a DAOS pool allocated across a number of ranks.
	PoolDestroy(ctx context.Context, in *PoolDestroyReq, opts ...grpc.CallOption) (*PoolDestroyResp, error)
	// Fetch the Access Control List for a DAOS pool.
	PoolGetACL(ctx context.Context, in *GetACLReq, opts ...grpc.CallOption) (*ACLResp, error)
	// Overwrite the Access Control List for a DAOS pool with a new one.
	PoolOverwriteACL(ctx context.Context, in *ModifyACLReq, opts ...grpc.CallOption) (*ACLResp, error)
	// Update existing the Access Control List for a DAOS pool with new entries.
	PoolUpdateACL(ctx context.Context, in *ModifyACLReq, opts ...grpc.CallOption) (*ACLResp, error)
	// Delete an entry from a DAOS pool's Access Control List.
	PoolDeleteACL(ctx context.Context, in *DeleteACLReq, opts ...grpc.CallOption) (*ACLResp, error)
	// Get the information required by libdaos to attach to the system.
	GetAttachInfo(ctx context.Context, in *GetAttachInfoReq, opts ...grpc.CallOption) (*GetAttachInfoResp, error)
	// Get BIO device health information.
	BioHealthQuery(ctx context.Context, in *BioHealthReq, opts ...grpc.CallOption) (*BioHealthResp, error)
	// Get SMD device list.
	SmdListDevs(ctx context.Context, in *SmdDevReq, opts ...grpc.CallOption) (*SmdDevResp, error)
	// Get SMD pool list.
	SmdListPools(ctx context.Context, in *SmdPoolReq, opts ...grpc.CallOption) (*SmdPoolResp, error)
	// Prepare DAOS IO server identified by rank for controlled shutdown.
	PrepShutdown(ctx context.Context, in *PrepShutdownReq, opts ...grpc.CallOption) (*DaosResp, error)
	// Kill DAOS IO server identified by rank.
	KillRank(ctx context.Context, in *KillRankReq, opts ...grpc.CallOption) (*DaosResp, error)
	// List all pools in a DAOS system: basic info: UUIDs, service ranks.
	ListPools(ctx context.Context, in *ListPoolsReq, opts ...grpc.CallOption) (*ListPoolsResp, error)
	// Get the current state of the device
	DevStateQuery(ctx context.Context, in *DevStateReq, opts ...grpc.CallOption) (*DevStateResp, error)
	// Set the device state of an NVMe SSD to FAULTY
	StorageSetFaulty(ctx context.Context, in *DevStateReq, opts ...grpc.CallOption) (*DevStateResp, error)
	// List all containers in a pool
	ListContainers(ctx context.Context, in *ListContReq, opts ...grpc.CallOption) (*ListContResp, error)
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

func (c *mgmtSvcClient) LeaderQuery(ctx context.Context, in *LeaderQueryReq, opts ...grpc.CallOption) (*LeaderQueryResp, error) {
	out := new(LeaderQueryResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/LeaderQuery", in, out, opts...)
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

func (c *mgmtSvcClient) PoolGetACL(ctx context.Context, in *GetACLReq, opts ...grpc.CallOption) (*ACLResp, error) {
	out := new(ACLResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/PoolGetACL", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) PoolOverwriteACL(ctx context.Context, in *ModifyACLReq, opts ...grpc.CallOption) (*ACLResp, error) {
	out := new(ACLResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/PoolOverwriteACL", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) PoolUpdateACL(ctx context.Context, in *ModifyACLReq, opts ...grpc.CallOption) (*ACLResp, error) {
	out := new(ACLResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/PoolUpdateACL", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) PoolDeleteACL(ctx context.Context, in *DeleteACLReq, opts ...grpc.CallOption) (*ACLResp, error) {
	out := new(ACLResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/PoolDeleteACL", in, out, opts...)
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

func (c *mgmtSvcClient) PrepShutdown(ctx context.Context, in *PrepShutdownReq, opts ...grpc.CallOption) (*DaosResp, error) {
	out := new(DaosResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/PrepShutdown", in, out, opts...)
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

func (c *mgmtSvcClient) DevStateQuery(ctx context.Context, in *DevStateReq, opts ...grpc.CallOption) (*DevStateResp, error) {
	out := new(DevStateResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/DevStateQuery", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) StorageSetFaulty(ctx context.Context, in *DevStateReq, opts ...grpc.CallOption) (*DevStateResp, error) {
	out := new(DevStateResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/StorageSetFaulty", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (c *mgmtSvcClient) ListContainers(ctx context.Context, in *ListContReq, opts ...grpc.CallOption) (*ListContResp, error) {
	out := new(ListContResp)
	err := c.cc.Invoke(ctx, "/mgmt.MgmtSvc/ListContainers", in, out, opts...)
	if err != nil {
		return nil, err
	}
	return out, nil
}

// MgmtSvcServer is the server API for MgmtSvc service.
type MgmtSvcServer interface {
	// Join the server described by JoinReq to the system.
	Join(context.Context, *JoinReq) (*JoinResp, error)
	// LeaderQuery provides a mechanism for clients to discover
	// the system's current Management Service leader
	LeaderQuery(context.Context, *LeaderQueryReq) (*LeaderQueryResp, error)
	// Create a DAOS pool allocated across a number of ranks
	PoolCreate(context.Context, *PoolCreateReq) (*PoolCreateResp, error)
	// Destroy a DAOS pool allocated across a number of ranks.
	PoolDestroy(context.Context, *PoolDestroyReq) (*PoolDestroyResp, error)
	// Fetch the Access Control List for a DAOS pool.
	PoolGetACL(context.Context, *GetACLReq) (*ACLResp, error)
	// Overwrite the Access Control List for a DAOS pool with a new one.
	PoolOverwriteACL(context.Context, *ModifyACLReq) (*ACLResp, error)
	// Update existing the Access Control List for a DAOS pool with new entries.
	PoolUpdateACL(context.Context, *ModifyACLReq) (*ACLResp, error)
	// Delete an entry from a DAOS pool's Access Control List.
	PoolDeleteACL(context.Context, *DeleteACLReq) (*ACLResp, error)
	// Get the information required by libdaos to attach to the system.
	GetAttachInfo(context.Context, *GetAttachInfoReq) (*GetAttachInfoResp, error)
	// Get BIO device health information.
	BioHealthQuery(context.Context, *BioHealthReq) (*BioHealthResp, error)
	// Get SMD device list.
	SmdListDevs(context.Context, *SmdDevReq) (*SmdDevResp, error)
	// Get SMD pool list.
	SmdListPools(context.Context, *SmdPoolReq) (*SmdPoolResp, error)
	// Prepare DAOS IO server identified by rank for controlled shutdown.
	PrepShutdown(context.Context, *PrepShutdownReq) (*DaosResp, error)
	// Kill DAOS IO server identified by rank.
	KillRank(context.Context, *KillRankReq) (*DaosResp, error)
	// List all pools in a DAOS system: basic info: UUIDs, service ranks.
	ListPools(context.Context, *ListPoolsReq) (*ListPoolsResp, error)
	// Get the current state of the device
	DevStateQuery(context.Context, *DevStateReq) (*DevStateResp, error)
	// Set the device state of an NVMe SSD to FAULTY
	StorageSetFaulty(context.Context, *DevStateReq) (*DevStateResp, error)
	// List all containers in a pool
	ListContainers(context.Context, *ListContReq) (*ListContResp, error)
}

// UnimplementedMgmtSvcServer can be embedded to have forward compatible implementations.
type UnimplementedMgmtSvcServer struct {
}

func (*UnimplementedMgmtSvcServer) Join(ctx context.Context, req *JoinReq) (*JoinResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method Join not implemented")
}
func (*UnimplementedMgmtSvcServer) LeaderQuery(ctx context.Context, req *LeaderQueryReq) (*LeaderQueryResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method LeaderQuery not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolCreate(ctx context.Context, req *PoolCreateReq) (*PoolCreateResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolCreate not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolDestroy(ctx context.Context, req *PoolDestroyReq) (*PoolDestroyResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolDestroy not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolGetACL(ctx context.Context, req *GetACLReq) (*ACLResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolGetACL not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolOverwriteACL(ctx context.Context, req *ModifyACLReq) (*ACLResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolOverwriteACL not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolUpdateACL(ctx context.Context, req *ModifyACLReq) (*ACLResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolUpdateACL not implemented")
}
func (*UnimplementedMgmtSvcServer) PoolDeleteACL(ctx context.Context, req *DeleteACLReq) (*ACLResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PoolDeleteACL not implemented")
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
func (*UnimplementedMgmtSvcServer) PrepShutdown(ctx context.Context, req *PrepShutdownReq) (*DaosResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method PrepShutdown not implemented")
}
func (*UnimplementedMgmtSvcServer) KillRank(ctx context.Context, req *KillRankReq) (*DaosResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method KillRank not implemented")
}
func (*UnimplementedMgmtSvcServer) ListPools(ctx context.Context, req *ListPoolsReq) (*ListPoolsResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method ListPools not implemented")
}
func (*UnimplementedMgmtSvcServer) DevStateQuery(ctx context.Context, req *DevStateReq) (*DevStateResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method DevStateQuery not implemented")
}
func (*UnimplementedMgmtSvcServer) StorageSetFaulty(ctx context.Context, req *DevStateReq) (*DevStateResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method StorageSetFaulty not implemented")
}
func (*UnimplementedMgmtSvcServer) ListContainers(ctx context.Context, req *ListContReq) (*ListContResp, error) {
	return nil, status.Errorf(codes.Unimplemented, "method ListContainers not implemented")
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

func _MgmtSvc_LeaderQuery_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(LeaderQueryReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).LeaderQuery(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/LeaderQuery",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).LeaderQuery(ctx, req.(*LeaderQueryReq))
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

func _MgmtSvc_PoolOverwriteACL_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(ModifyACLReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).PoolOverwriteACL(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/PoolOverwriteACL",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).PoolOverwriteACL(ctx, req.(*ModifyACLReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_PoolUpdateACL_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(ModifyACLReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).PoolUpdateACL(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/PoolUpdateACL",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).PoolUpdateACL(ctx, req.(*ModifyACLReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_PoolDeleteACL_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(DeleteACLReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).PoolDeleteACL(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/PoolDeleteACL",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).PoolDeleteACL(ctx, req.(*DeleteACLReq))
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

func _MgmtSvc_PrepShutdown_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(PrepShutdownReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).PrepShutdown(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/PrepShutdown",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).PrepShutdown(ctx, req.(*PrepShutdownReq))
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

func _MgmtSvc_DevStateQuery_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(DevStateReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).DevStateQuery(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/DevStateQuery",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).DevStateQuery(ctx, req.(*DevStateReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_StorageSetFaulty_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(DevStateReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).StorageSetFaulty(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/StorageSetFaulty",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).StorageSetFaulty(ctx, req.(*DevStateReq))
	}
	return interceptor(ctx, in, info, handler)
}

func _MgmtSvc_ListContainers_Handler(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
	in := new(ListContReq)
	if err := dec(in); err != nil {
		return nil, err
	}
	if interceptor == nil {
		return srv.(MgmtSvcServer).ListContainers(ctx, in)
	}
	info := &grpc.UnaryServerInfo{
		Server:     srv,
		FullMethod: "/mgmt.MgmtSvc/ListContainers",
	}
	handler := func(ctx context.Context, req interface{}) (interface{}, error) {
		return srv.(MgmtSvcServer).ListContainers(ctx, req.(*ListContReq))
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
			MethodName: "LeaderQuery",
			Handler:    _MgmtSvc_LeaderQuery_Handler,
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
			MethodName: "PoolOverwriteACL",
			Handler:    _MgmtSvc_PoolOverwriteACL_Handler,
		},
		{
			MethodName: "PoolUpdateACL",
			Handler:    _MgmtSvc_PoolUpdateACL_Handler,
		},
		{
			MethodName: "PoolDeleteACL",
			Handler:    _MgmtSvc_PoolDeleteACL_Handler,
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
			MethodName: "PrepShutdown",
			Handler:    _MgmtSvc_PrepShutdown_Handler,
		},
		{
			MethodName: "KillRank",
			Handler:    _MgmtSvc_KillRank_Handler,
		},
		{
			MethodName: "ListPools",
			Handler:    _MgmtSvc_ListPools_Handler,
		},
		{
			MethodName: "DevStateQuery",
			Handler:    _MgmtSvc_DevStateQuery_Handler,
		},
		{
			MethodName: "StorageSetFaulty",
			Handler:    _MgmtSvc_StorageSetFaulty_Handler,
		},
		{
			MethodName: "ListContainers",
			Handler:    _MgmtSvc_ListContainers_Handler,
		},
	},
	Streams:  []grpc.StreamDesc{},
	Metadata: "mgmt.proto",
}
