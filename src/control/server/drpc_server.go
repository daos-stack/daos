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

package server

import (
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
)

// drpcServer represents the daos_server dRPC module. It handles dRPCs sent by
// the daos_io_server iosrv module (src/iosrv).
type drpcServer struct {
	iosrv *IOServerInstance
}

// HandleCall is the handler for calls to the drpcServer
func (mod *drpcServer) HandleCall(cli *drpc.Client, method int32, req []byte) ([]byte, error) {
	switch method {
	case notifyReady:
		return nil, mod.handleNotifyReady(req)
	default:
		return nil, errors.Errorf("unknown dRPC %d", method)
	}
}

func (mod *drpcServer) InitModule(state drpc.ModuleState) {}

func (mod *drpcServer) ID() int32 {
	return drpcServerID
}

func (mod *drpcServer) handleNotifyReady(reqb []byte) error {
	req := &srvpb.NotifyReadyReq{}
	if err := proto.Unmarshal(reqb, req); err != nil {
		return errors.Wrap(err, "unmarshal NotifyReady request")
	}

	mod.iosrv.NotifyReady(req)

	return nil
}
