//
// (C) Copyright 2019 Intel Corporation.
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
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

// mgmtModule represents the daos_agent dRPC module. It acts mostly as a
// Management Service proxy, handling dRPCs sent by libdaos by forwarding them
// to MS.
type mgmtModule struct {
	log logging.Logger
	// The access point
	ap   string
	tcfg *security.TransportConfig
}

func (mod *mgmtModule) HandleCall(cli *drpc.Client, method int32, req []byte) ([]byte, error) {
	switch method {
	case drpc.MethodGetAttachInfo:
		return mod.handleGetAttachInfo(req)
	default:
		return nil, errors.Errorf("unknown dRPC %d", method)
	}
}

func (mod *mgmtModule) InitModule(state drpc.ModuleState) {}

func (mod *mgmtModule) ID() int32 {
	return drpc.ModuleMgmt
}

func (mod *mgmtModule) handleGetAttachInfo(reqb []byte) ([]byte, error) {
	req := &mgmtpb.GetAttachInfoReq{}
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, errors.Wrap(err, "unmarshal GetAttachInfo request")
	}

	mod.log.Debugf("GetAttachInfo %s %v", mod.ap, *req)

	dialOpt, err := security.DialOptionForTransportConfig(mod.tcfg)
	if err != nil {
		return nil, err
	}
	opts := []grpc.DialOption{dialOpt}

	conn, err := grpc.Dial(mod.ap, opts...)
	if err != nil {
		return nil, errors.Wrapf(err, "dial %s", mod.ap)
	}
	defer conn.Close()

	client := mgmtpb.NewMgmtSvcClient(conn)

	resp, err := client.GetAttachInfo(context.Background(), req)
	if err != nil {
		return nil, errors.Wrapf(err, "GetAttachInfo %s %v", mod.ap, *req)
	}

	resmgmtpb, err := proto.Marshal(resp)
	if err != nil {
		return nil, errors.Wrap(err, "marshal GetAttachInfo response")
	}

	return resmgmtpb, nil
}
