//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
)

// ListContainers forwards a gRPC request to the DAOS IO server to retrieve a pool's
// list of containers.
func (svc *mgmtSvc) ListContainers(ctx context.Context, req *mgmtpb.ListContReq) (*mgmtpb.ListContResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.ListContainers dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodListContainers, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ListContResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal ListContainers response")
	}

	svc.log.Debugf("MgmtSvc.ListContainers dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// ContSetOwner forwards a gRPC request to the DAOS IO server to change a container's ownership.
func (svc *mgmtSvc) ContSetOwner(ctx context.Context, req *mgmtpb.ContSetOwnerReq) (*mgmtpb.ContSetOwnerResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}
	svc.log.Debugf("MgmtSvc.ContSetOwner dispatch, req:%+v\n", *req)

	dresp, err := svc.harness.CallDrpc(ctx, drpc.MethodContSetOwner, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ContSetOwnerResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal ContSetOwner response")
	}

	svc.log.Debugf("MgmtSvc.ContSetOwner dispatch, resp:%+v\n", *resp)

	return resp, nil
}
