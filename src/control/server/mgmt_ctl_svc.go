//
// (C) Copyright 2018-2019 Intel Corporation.
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

package main

import (
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
)

// ControlService mgmt methods forward gRPC request from management tool to
// iosrv via dRPC channel. Usually on host identified as first access point.

// callDrpcMethodWithMessage create a new drpc Call instance, open a
// drpc connection, send a message with the protobuf message marshalled
// in the body, and closes the connection. Returns unmarshalled response.
func (c *controlService) callDrpcMethodWithMessage(
	methodID int32, body proto.Message) (resp *pb.DaosResponse, err error) {

	drpcResp, err := makeDrpcCall(c.drpc, mgmtModuleID, methodID, body)
	if err != nil {
		return nil, errors.WithStack(err)
	}

	// unmarshal daos response message returned in drpc response body
	resp = &pb.DaosResponse{}
	err = proto.Unmarshal(drpcResp.Body, resp)
	if err != nil {
		return nil, errors.Errorf("invalid dRPC response body: %v", err)
	}

	return
}

// KillRank implements the method defined for the MgmtControl protobuf service.
func (c *controlService) KillRank(
	ctx context.Context, rank *pb.DaosRank) (*pb.DaosResponse, error) {

	log.Debugf("ControlService.KillRank dispatch\n")

	return c.callDrpcMethodWithMessage(killRank, rank)
}
