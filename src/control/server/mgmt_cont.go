//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

// ListContainers forwards a gRPC request to the DAOS I/O Engine to retrieve a pool's
// list of containers.
func (svc *mgmtSvc) ListContainers(ctx context.Context, req *mgmtpb.ListContReq) (*mgmtpb.ListContResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}

	dresp, err := svc.makePoolServiceCall(ctx, daos.MethodListContainers, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ListContResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal ListContainers response")
	}

	return resp, nil
}

// ContSetOwner forwards a gRPC request to the DAOS I/O Engine to change a container's ownership.
func (svc *mgmtSvc) ContSetOwner(ctx context.Context, req *mgmtpb.ContSetOwnerReq) (*mgmtpb.DaosResp, error) {
	if err := svc.checkReplicaRequest(req); err != nil {
		return nil, err
	}

	dresp, err := svc.makePoolServiceCall(ctx, daos.MethodContSetOwner, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.DaosResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal ContSetOwner response")
	}

	return resp, nil
}
