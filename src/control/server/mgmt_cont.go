//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
