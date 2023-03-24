//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	pbUtil "github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// PoolGetACLReq contains the input parameters for PoolGetACL
type PoolGetACLReq struct {
	unaryRequest
	msRequest
	ID string // pool ID
}

// PoolGetACLResp contains the output results for PoolGetACL
type PoolGetACLResp struct {
	ACL *AccessControlList
}

// PoolGetACL gets the Access Control List for the pool.
func PoolGetACL(ctx context.Context, rpcClient UnaryInvoker, req *PoolGetACLReq) (*PoolGetACLResp, error) {
	pbReq := &mgmtpb.GetACLReq{
		Sys: req.getSystem(rpcClient),
		Id:  req.ID,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolGetACL(ctx, pbReq)
	})

	rpcClient.Debugf("Get DAOS pool ACL request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pgar := &PoolGetACLResp{
		ACL: &AccessControlList{},
	}
	return pgar, convertMSResponse(ur, pgar)
}

// PoolOverwriteACLReq contains the input parameters for PoolOverwriteACL
type PoolOverwriteACLReq struct {
	unaryRequest
	msRequest
	ID  string             // pool UUID or label
	ACL *AccessControlList // new ACL for the pool
}

// PoolOverwriteACLResp returns the updated ACL for the pool
type PoolOverwriteACLResp struct {
	ACL *AccessControlList // actual ACL of the pool
}

// PoolOverwriteACL sends a request to replace the pool's old Access Control List
// with a new one. If it succeeds, it returns the updated ACL. If not, it returns
// an error.
func PoolOverwriteACL(ctx context.Context, rpcClient UnaryInvoker, req *PoolOverwriteACLReq) (*PoolOverwriteACLResp, error) {
	if req.ACL.Empty() {
		return nil, errors.New("empty ACL on modify")
	}

	pbReq := &mgmtpb.ModifyACLReq{
		Sys:     req.getSystem(rpcClient),
		Id:      req.ID,
		Entries: req.ACL.Entries,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolOverwriteACL(ctx, pbReq)
	})

	rpcClient.Debugf("Overwrite DAOS pool ACL request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	poar := &PoolOverwriteACLResp{
		ACL: &AccessControlList{},
	}
	return poar, convertMSResponse(ur, poar)

}

// PoolUpdateACLReq contains the input parameters for PoolUpdateACL
type PoolUpdateACLReq struct {
	unaryRequest
	msRequest
	ID  string             // pool UUID or label
	ACL *AccessControlList // ACL entries to add to the pool
}

// PoolUpdateACLResp returns the updated ACL for the pool
type PoolUpdateACLResp struct {
	ACL *AccessControlList // actual ACL of the pool
}

// PoolUpdateACL sends a request to add new entries and update existing entries
// in a pool's Access Control List. If it succeeds, it returns the updated ACL.
// If not, it returns an error.
func PoolUpdateACL(ctx context.Context, rpcClient UnaryInvoker, req *PoolUpdateACLReq) (*PoolUpdateACLResp, error) {
	if req.ACL.Empty() {
		return nil, errors.New("empty ACL on modify")
	}

	pbReq := &mgmtpb.ModifyACLReq{
		Sys:     req.getSystem(rpcClient),
		Id:      req.ID,
		Entries: req.ACL.Entries,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolUpdateACL(ctx, pbReq)
	})

	rpcClient.Debugf("Update DAOS pool ACL request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	puar := &PoolUpdateACLResp{
		ACL: &AccessControlList{},
	}
	return puar, convertMSResponse(ur, puar)
}

// PoolDeleteACLReq contains the input parameters for PoolDeleteACL.
type PoolDeleteACLReq struct {
	unaryRequest
	msRequest
	ID        string // UUID or label of the pool
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

	pbReq := &mgmtpb.DeleteACLReq{
		Sys:       req.getSystem(rpcClient),
		Id:        req.ID,
		Principal: req.Principal,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDeleteACL(ctx, pbReq)
	})

	rpcClient.Debugf("Delete DAOS pool ACL request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pdar := &PoolDeleteACLResp{
		ACL: &AccessControlList{},
	}
	return pdar, convertMSResponse(ur, pdar)
}
