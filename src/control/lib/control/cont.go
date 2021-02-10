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

// ContSetOwnerReq contains the parameters for the set owner request
type ContSetOwnerReq struct {
	msRequest
	unaryRequest
	ContUUID string // Container UUID
	PoolUUID string // UUID of the pool for the container
	User     string // User to own the container, or empty if none
	Group    string // Group to own the container, or empty if none
}

// ContSetOwner changes the owner user and/or group of a DAOS container.
func ContSetOwner(ctx context.Context, rpcClient UnaryInvoker, req *ContSetOwnerReq) error {
	if req == nil {
		return errors.New("nil request")
	}

	if err := checkUUID(req.ContUUID); err != nil {
		return err
	}

	if err := checkUUID(req.PoolUUID); err != nil {
		return err
	}

	if req.User == "" && req.Group == "" {
		return errors.New("no user or group specified")
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).ContSetOwner(ctx, &mgmtpb.ContSetOwnerReq{
			Sys:        req.getSystem(),
			ContUUID:   req.ContUUID,
			PoolUUID:   req.PoolUUID,
			Owneruser:  req.User,
			Ownergroup: req.Group,
		})
	})

	rpcClient.Debugf("Set DAOS container owner request: %+v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "container set-owner failed")
	}

	rpcClient.Debugf("Set DAOS container owner response: %+v\n", msResp)

	return nil
}
