//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// ContSetOwnerReq contains the parameters for the set owner request
type ContSetOwnerReq struct {
	msRequest
	unaryRequest
	ContID string // Container UUID or label
	PoolID string // UUID or label of the pool for the container
	User   string // User to own the container, or empty if none
	Group  string // Group to own the container, or empty if none
}

// ContSetOwner changes the owner user and/or group of a DAOS container.
func ContSetOwner(ctx context.Context, rpcClient UnaryInvoker, req *ContSetOwnerReq) error {
	if req == nil {
		return errors.New("nil request")
	}

	if req.PoolID == "" {
		return errors.New("no pool label or UUID specified")
	}

	if req.ContID == "" {
		return errors.New("no container label or UUID specified")
	}

	if req.User == "" && req.Group == "" {
		return errors.New("no user or group specified")
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).ContSetOwner(ctx, &mgmtpb.ContSetOwnerReq{
			Sys:        req.getSystem(rpcClient),
			ContId:     req.ContID,
			PoolId:     req.PoolID,
			OwnerUser:  req.User,
			OwnerGroup: req.Group,
		})
	})

	rpcClient.Debugf("Set DAOS container owner request: %+v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "container set-owner failed")
}
