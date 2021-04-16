//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// PoolGetACLReq contains the input parameters for PoolGetACL
type PoolGetACLReq struct {
	unaryRequest
	msRequest
	UUID string // pool UUID
}

// PoolGetACLResp contains the output results for PoolGetACL
type PoolGetACLResp struct {
	ACL *AccessControlList
}

// PoolGetACL gets the Access Control List for the pool.
func PoolGetACL(ctx context.Context, rpcClient UnaryInvoker, req *PoolGetACLReq) (*PoolGetACLResp, error) {
	if err := checkUUID(req.UUID); err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolGetACL(ctx, &mgmtpb.GetACLReq{
			Sys:  req.getSystem(),
			Uuid: req.UUID,
		})
	})

	rpcClient.Debugf("Get DAOS pool ACL request: %+v\n", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pgar := &PoolGetACLResp{
		ACL: &AccessControlList{},
	}
	return pgar, convertMSResponse(ur, pgar.ACL)
}

// PoolOverwriteACLReq contains the input parameters for PoolOverwriteACL
type PoolOverwriteACLReq struct {
	unaryRequest
	msRequest
	UUID string             // pool UUID
	ACL  *AccessControlList // new ACL for the pool
}

// PoolOverwriteACLResp returns the updated ACL for the pool
type PoolOverwriteACLResp struct {
	ACL *AccessControlList // actual ACL of the pool
}

// PoolOverwriteACL sends a request to replace the pool's old Access Control List
// with a new one. If it succeeds, it returns the updated ACL. If not, it returns
// an error.
func PoolOverwriteACL(ctx context.Context, rpcClient UnaryInvoker, req *PoolOverwriteACLReq) (*PoolOverwriteACLResp, error) {
	if err := checkUUID(req.UUID); err != nil {
		return nil, err
	}
	if req.ACL.Empty() {
		return nil, errors.New("empty ACL on modify")
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolOverwriteACL(ctx, &mgmtpb.ModifyACLReq{
			Sys:  req.getSystem(),
			Uuid: req.UUID,
			ACL:  req.ACL.Entries,
		})
	})

	rpcClient.Debugf("Overwrite DAOS pool ACL request: %+v\n", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	poar := &PoolOverwriteACLResp{
		ACL: &AccessControlList{},
	}
	return poar, convertMSResponse(ur, poar.ACL)

}

// PoolUpdateACLReq contains the input parameters for PoolUpdateACL
type PoolUpdateACLReq struct {
	unaryRequest
	msRequest
	UUID string             // pool UUID
	ACL  *AccessControlList // ACL entries to add to the pool
}

// PoolUpdateACLResp returns the updated ACL for the pool
type PoolUpdateACLResp struct {
	ACL *AccessControlList // actual ACL of the pool
}

// PoolUpdateACL sends a request to add new entries and update existing entries
// in a pool's Access Control List. If it succeeds, it returns the updated ACL.
// If not, it returns an error.
func PoolUpdateACL(ctx context.Context, rpcClient UnaryInvoker, req *PoolUpdateACLReq) (*PoolUpdateACLResp, error) {
	if err := checkUUID(req.UUID); err != nil {
		return nil, err
	}
	if req.ACL.Empty() {
		return nil, errors.New("empty ACL on modify")
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolUpdateACL(ctx, &mgmtpb.ModifyACLReq{
			Sys:  req.getSystem(),
			Uuid: req.UUID,
			ACL:  req.ACL.Entries,
		})
	})

	rpcClient.Debugf("Update DAOS pool ACL request: %+v\n", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	puar := &PoolUpdateACLResp{
		ACL: &AccessControlList{},
	}
	return puar, convertMSResponse(ur, puar.ACL)
}

// PoolDeleteACLReq contains the input parameters for PoolDeleteACL.
type PoolDeleteACLReq struct {
	unaryRequest
	msRequest
	UUID      string // UUID of the pool
	Principal string // Principal whose entry will be removed
}

// PoolDeleteACLResp returns the updated ACL for the pool.
type PoolDeleteACLResp struct {
	ACL *AccessControlList // actual ACL of the pool
}

// PoolDeleteACL sends a request to delete an entry in a pool's Access Control
// List. If it succeeds, it returns the updated ACL. If it fails, it returns an
// error.
func PoolDeleteACL(ctx context.Context, rpcClient UnaryInvoker, req *PoolDeleteACLReq) (*PoolDeleteACLResp, error) {
	if req.Principal == "" {
		return nil, errors.New("no principal provided")
	}

	if err := checkUUID(req.UUID); err != nil {
		return nil, err
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDeleteACL(ctx, &mgmtpb.DeleteACLReq{
			Sys:       req.getSystem(),
			Uuid:      req.UUID,
			Principal: req.Principal,
		})
	})

	rpcClient.Debugf("Delete DAOS pool ACL request: %+v\n", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pdar := &PoolDeleteACLResp{
		ACL: &AccessControlList{},
	}
	return pdar, convertMSResponse(ur, pdar.ACL)
}
